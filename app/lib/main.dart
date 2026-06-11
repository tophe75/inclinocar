import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:shared_preferences/shared_preferences.dart';

const String NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const String NUS_TX      = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';
const String NUS_RX      = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';

const MethodChannel _wakelockChannel = MethodChannel('com.inclinocar/wakelock');

const Color kBg       = Color(0xFF0D1A0D);
const Color kCard     = Color(0xFF111E11);
const Color kGreen    = Color(0xFF4CAF50);
const Color kGreenDim = Color(0xFF2E7D32);
const Color kText     = Color(0xFFC8E6C8);
const Color kDim      = Color(0xFF5A8A5A);
const Color kBorder   = Color(0xFF1E3A1E);
const Color kAmber    = Color(0xFFFFB300);
const Color kRed      = Color(0xFFEF5350);

const String kAppVersion = '0.0.31';

class KnownDevice {
  final String mac;
  final String nickname;
  KnownDevice({required this.mac, required this.nickname});
  Map<String, String> toMap() => {'mac': mac, 'nickname': nickname};
  factory KnownDevice.fromMap(Map<String, dynamic> m) =>
    KnownDevice(mac: m['mac'], nickname: m['nickname']);
  String toJson() => jsonEncode(toMap());
  factory KnownDevice.fromJson(String s) => KnownDevice.fromMap(jsonDecode(s));
}

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setPreferredOrientations([DeviceOrientation.portraitUp]);
  SystemChrome.setSystemUIOverlayStyle(const SystemUiOverlayStyle(
    statusBarColor: Colors.transparent,
    statusBarIconBrightness: Brightness.light,
  ));
  runApp(const InclinoCarApp());
}

class InclinoCarApp extends StatelessWidget {
  const InclinoCarApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'InclinoCar',
    debugShowCheckedModeBanner: false,
    theme: ThemeData.dark().copyWith(
      scaffoldBackgroundColor: kBg,
      colorScheme: const ColorScheme.dark(primary: kGreen, surface: kCard),
    ),
    home: const HomePage(),
  );
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});
  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  BluetoothDevice?         _device;
  BluetoothCharacteristic? _txChar;
  BluetoothCharacteristic? _rxChar;
  StreamSubscription?      _scanSub;
  StreamSubscription?      _dataSub;
  StreamSubscription?      _connSub;

  bool   _scanning    = false;
  bool   _connected   = false;
  bool   _pinVerified = false;
  bool   _wakeLock    = true;
  String _status      = 'Not connected';
  String _nickname    = '';
  String _connectedMac = '';

  double _pitch = 0.0;
  double _roll  = 0.0;

  List<KnownDevice> _knownDevices  = [];
  String?           _preferredMac;
  List<String>      _scanLog        = [];

  @override
  void initState() {
    super.initState();
    _wakelockChannel.invokeMethod('enable');
    _loadPrefs().then((_) {
      if (_preferredMac != null) _autoConnect();
    });
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    _dataSub?.cancel();
    _connSub?.cancel();
    _wakelockChannel.invokeMethod('disable');
    super.dispose();
  }

  // ── Prefs ────────────────────────────────────────────────────
  Future<void> _loadPrefs() async {
    final prefs = await SharedPreferences.getInstance();
    final mac   = prefs.getString('preferred_mac');
    final list  = prefs.getStringList('known_devices') ?? [];
    setState(() {
      _preferredMac = mac;
      _knownDevices = list.map((s) => KnownDevice.fromJson(s)).toList();
      if (mac != null) {
        final known = _knownDevices.where((d) => d.mac == mac).firstOrNull;
        if (known != null) _nickname = known.nickname;
      }
    });
  }

  Future<void> _saveDevice(String mac, String nickname) async {
    final prefs = await SharedPreferences.getInstance();
    _knownDevices.removeWhere((d) => d.mac == mac);
    _knownDevices.insert(0, KnownDevice(mac: mac, nickname: nickname));
    await prefs.setString('preferred_mac', mac);
    await prefs.setStringList(
      'known_devices', _knownDevices.map((d) => d.toJson()).toList());
    setState(() => _preferredMac = mac);
  }

  Future<void> _removeDevice(String mac) async {
    final prefs = await SharedPreferences.getInstance();
    _knownDevices.removeWhere((d) => d.mac == mac);
    await prefs.setStringList(
      'known_devices', _knownDevices.map((d) => d.toJson()).toList());
    if (_preferredMac == mac) {
      _preferredMac = _knownDevices.isNotEmpty ? _knownDevices.first.mac : null;
      if (_preferredMac != null) {
        await prefs.setString('preferred_mac', _preferredMac!);
      } else {
        await prefs.remove('preferred_mac');
      }
    }
    setState(() {});
  }

  void _addLog(String msg) {
    if (mounted) setState(() {
      _scanLog.insert(0, msg);
      if (_scanLog.length > 20) _scanLog.removeLast();
    });
  }

  Future<void> _requestPermissions() async {
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
    ].request();
  }

  // ── Auto-connect to known MAC (no PIN needed for known devices) ──
  Future<void> _autoConnect() async {
    await _requestPermissions();
    setState(() { _scanning = true; _status = 'Scanning...'; });

    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));

    _scanSub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        final mac     = r.device.remoteId.toString();
        final name    = r.device.platformName;
        final advName = r.advertisementData.advName;
        debugPrint('Auto scan: $mac name="$name" advName="$advName"');
        if (mac == _preferredMac) {
          FlutterBluePlus.stopScan();
          _connectAndSkipPin(r.device);
          break;
        }
      }
    });

    FlutterBluePlus.isScanning.listen((scanning) {
      if (!scanning && mounted && !_connected) {
        setState(() { _scanning = false; _status = 'Device not found'; });
      }
    });
  }

  // ── Manual scan — show all InclinoCore devices ───────────────
  Future<void> _manualScan() async {
    await _requestPermissions();
    final found = <ScanResult>[];
    setState(() { _scanning = true; _scanLog.clear(); });

    // Use subscription — more reliable than await for on all Android versions
    StreamSubscription? sub;
    sub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        final mac     = r.device.remoteId.toString();
        final name    = r.device.platformName;
        final advName = r.advertisementData.advName;
        final isCore  = name == 'InclinoCore' || advName == 'InclinoCore';
        final isKnown = _knownDevices.any((d) => d.mac == mac);
        _addLog('${isCore||isKnown?"✓":"·"} $mac "${name.isNotEmpty?name:advName}"');
        if ((isCore || isKnown) &&
            !found.any((e) => e.device.remoteId == r.device.remoteId)) {
          found.add(r);
        }
      }
    });

    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 8));

    // Wait for scan to finish
    await FlutterBluePlus.isScanning.where((s) => s == false).first
      .timeout(const Duration(seconds: 10), onTimeout: () => false);

    sub.cancel();
    await FlutterBluePlus.stopScan();
    setState(() => _scanning = false);
    if (!mounted) return;

    if (found.isEmpty) {
      // Show what was found for debugging
      await showDialog(
        context: context,
        builder: (ctx) => AlertDialog(
          backgroundColor: kCard,
          title: const Text('No InclinoCore found',
            style: TextStyle(color: kText, fontSize: 15)),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Devices seen during scan:',
                style: TextStyle(color: kDim, fontSize: 12)),
              const SizedBox(height: 8),
              if (_scanLog.isEmpty)
                Text('Nothing found at all',
                  style: TextStyle(color: kRed, fontSize: 12))
              else
                ..._scanLog.take(10).map((l) => Padding(
                  padding: const EdgeInsets.symmetric(vertical: 1),
                  child: Text(l,
                    style: TextStyle(color: kDim, fontSize: 10,
                      fontFamily: 'monospace')))),
            ],
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx),
              child: Text('OK', style: TextStyle(color: kGreen))),
          ],
        ),
      );
      return;
    }

    // Show device picker
    final picked = await showDialog<BluetoothDevice>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: kCard,
        title: const Text('Select InclinoCore',
          style: TextStyle(color: kText, fontSize: 16)),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: found.map((r) {
            final mac   = r.device.remoteId.toString();
            final known = _knownDevices.where((d) => d.mac == mac).firstOrNull;
            final isKnown = known != null;
            return ListTile(
              leading: Icon(
                isKnown ? Icons.star : Icons.bluetooth,
                color: isKnown ? kGreen : kDim, size: 20),
              title: Text(
                known?.nickname ?? r.device.platformName,
                style: const TextStyle(color: kText, fontSize: 14)),
              subtitle: Text(mac,
                style: TextStyle(color: kDim, fontSize: 11)),
              trailing: Text('${r.rssi} dBm',
                style: TextStyle(color: kDim, fontSize: 11)),
              onTap: () => Navigator.pop(ctx, r.device),
            );
          }).toList(),
        ),
      ),
    );

    if (picked == null) return;

    final mac     = picked.remoteId.toString();
    final isKnown = _knownDevices.any((d) => d.mac == mac);

    if (isKnown) {
      // Known device — skip PIN
      _connectAndSkipPin(picked);
    } else {
      // New device — ask for PIN
      _connectWithPin(picked);
    }
  }

  // ── Connect known device (no PIN prompt) ─────────────────────
  Future<void> _connectAndSkipPin(BluetoothDevice device) async {
    await _connectBLE(device);
    if (_connected) {
      // Send a bypass token for known devices
      await Future.delayed(const Duration(milliseconds: 500));
      await _sendRaw('KNOWNMAC:${device.remoteId}');
    }
  }

  // ── Connect new device with PIN prompt ───────────────────────
  Future<void> _connectWithPin(BluetoothDevice device) async {
    // Show PIN dialog first
    final pin = await _showPinDialog(device.remoteId.toString());
    if (pin == null) return;

    await _connectBLE(device);
    if (!_connected) return;

    // Send PIN
    await Future.delayed(const Duration(milliseconds: 500));
    await _sendRaw('PIN:$pin');
  }

  Future<String?> _showPinDialog(String mac) async {
    final ctrl = TextEditingController();
    return showDialog<String>(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => AlertDialog(
        backgroundColor: kCard,
        title: const Text('Enter PIN', style: TextStyle(color: kText)),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Check the InclinoCore display for the 4-digit PIN.',
              style: TextStyle(color: kDim, fontSize: 13)),
            const SizedBox(height: 8),
            Text(mac, style: TextStyle(color: kDim, fontSize: 10)),
            const SizedBox(height: 16),
            TextField(
              controller: ctrl,
              keyboardType: TextInputType.number,
              maxLength: 4,
              autofocus: true,
              style: const TextStyle(color: kText, fontSize: 24,
                letterSpacing: 8, fontWeight: FontWeight.w300),
              textAlign: TextAlign.center,
              decoration: InputDecoration(
                hintText: '0000',
                hintStyle: TextStyle(color: kDim, letterSpacing: 8),
                counterText: '',
                enabledBorder: OutlineInputBorder(
                  borderSide: BorderSide(color: kBorder),
                  borderRadius: BorderRadius.circular(8)),
                focusedBorder: OutlineInputBorder(
                  borderSide: BorderSide(color: kGreen),
                  borderRadius: BorderRadius.circular(8)),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx),
            child: Text('Cancel', style: TextStyle(color: kDim))),
          TextButton(
            onPressed: () {
              if (ctrl.text.length == 4) Navigator.pop(ctx, ctrl.text);
            },
            child: Text('Connect', style: TextStyle(color: kGreen))),
        ],
      ),
    );
  }

  // ── Core BLE connect ─────────────────────────────────────────
  Future<void> _connectBLE(BluetoothDevice device) async {
    setState(() { _status = 'Connecting...'; });
    _device = device;
    _connectedMac = device.remoteId.toString();

    try {
      await device.connect(timeout: const Duration(seconds: 10));

      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          setState(() {
            _connected   = false;
            _pinVerified = false;
            _status      = 'Disconnected';
            _pitch       = 0.0;
            _roll        = 0.0;
          });
          _dataSub?.cancel();
        }
      });

      final services = await device.discoverServices();
      for (final s in services) {
        if (s.serviceUuid.toString().toLowerCase() == NUS_SERVICE) {
          for (final c in s.characteristics) {
            final uuid = c.characteristicUuid.toString().toLowerCase();
            if (uuid == NUS_TX) {
              _txChar = c;
              await c.setNotifyValue(true);
              _dataSub = c.onValueReceived.listen(_onData);
            }
            if (uuid == NUS_RX) _rxChar = c;
          }
        }
      }

      setState(() { _connected = true; _status = 'Verifying...'; });
    } catch (e) {
      setState(() => _status = 'Connection failed');
    }
  }

  Future<void> _disconnect() async {
    await _device?.disconnect();
    _scanSub?.cancel();
    _dataSub?.cancel();
    _connSub?.cancel();
    setState(() {
      _connected   = false;
      _pinVerified = false;
      _status      = 'Not connected';
      _pitch       = 0.0;
      _roll        = 0.0;
    });
  }

  void _onData(List<int> data) {
    final str = utf8.decode(data).trim();
    try {
      final json = jsonDecode(str) as Map<String, dynamic>;

      // PIN response
      if (json['pin'] != null) {
        if (json['pin'] == 'ok') {
          setState(() { _pinVerified = true; _status = 'Connected'; });
          final nick = json['n'] as String? ?? 'InclinoCore';
          setState(() => _nickname = nick);
          _saveDevice(_connectedMac, nick);
        } else {
          setState(() => _status = 'Wrong PIN');
          ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
            content: Text('Wrong PIN — check the display and try again'),
            backgroundColor: kRed));
          _disconnect();
        }
        return;
      }

      // Data stream
      if (json['p'] != null && _pinVerified && mounted) {
        setState(() {
          _pitch = (json['p'] as num).toDouble();
          _roll  = (json['r'] as num).toDouble();
          if (json['n'] != null) _nickname = json['n'] as String;
        });
      }
    } catch (_) {}
  }

  Future<void> _sendRaw(String cmd) async {
    if (_rxChar == null) return;
    try {
      await _rxChar!.write(utf8.encode('$cmd\n'), withoutResponse: true);
    } catch (e) { debugPrint('BLE write: $e'); }
  }

  Future<void> _sendCommand(String cmd) async {
    if (_rxChar == null || !_pinVerified) return;
    await _sendRaw(cmd);
  }

  Future<void> _confirmCalibrate() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: kCard,
        title: const Text('Calibrate?', style: TextStyle(color: kText)),
        content: const Text(
          'Place the vehicle on flat level ground and keep it still.\n\nAre you sure?',
          style: TextStyle(color: kDim, fontSize: 14)),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false),
            child: Text('Cancel', style: TextStyle(color: kDim))),
          TextButton(onPressed: () => Navigator.pop(ctx, true),
            child: Text('Calibrate', style: TextStyle(color: kGreen))),
        ],
      ),
    );
    if (ok == true) {
      await _sendCommand('CAL');
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
        content: Text('Calibrating — keep device still for 2 seconds'),
        backgroundColor: kGreenDim, duration: Duration(seconds: 3)));
    }
  }

  Future<void> _setNickname() async {
    final ctrl = TextEditingController(text: _nickname);
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: kCard,
        title: const Text('Device Nickname', style: TextStyle(color: kText)),
        content: TextField(
          controller: ctrl, maxLength: 20,
          style: const TextStyle(color: kText),
          decoration: InputDecoration(
            hintText: 'e.g. Red Defender',
            hintStyle: TextStyle(color: kDim),
            enabledBorder: UnderlineInputBorder(
              borderSide: BorderSide(color: kBorder)),
            focusedBorder: UnderlineInputBorder(
              borderSide: BorderSide(color: kGreen)),
            counterStyle: TextStyle(color: kDim),
          ),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx),
            child: Text('Cancel', style: TextStyle(color: kDim))),
          TextButton(onPressed: () => Navigator.pop(ctx, ctrl.text.trim()),
            child: Text('Save', style: TextStyle(color: kGreen))),
        ],
      ),
    );
    if (result != null && result.isNotEmpty) {
      await _sendCommand('NICK:$result');
      setState(() => _nickname = result);
      await _saveDevice(_connectedMac, result);
    }
  }

  void _showKnownDevices() {
    showModalBottomSheet(
      context: context,
      backgroundColor: kCard,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(16))),
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setSS) => Padding(
          padding: const EdgeInsets.symmetric(vertical: 12),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(width: 40, height: 4,
                margin: const EdgeInsets.only(bottom: 12),
                decoration: BoxDecoration(
                  color: kBorder, borderRadius: BorderRadius.circular(2))),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                child: Text('Known Devices',
                  style: TextStyle(color: kText, fontSize: 14,
                    fontWeight: FontWeight.w500))),
              if (_knownDevices.isEmpty)
                Padding(padding: const EdgeInsets.all(24),
                  child: Text('No saved devices',
                    style: TextStyle(color: kDim, fontSize: 13)))
              else
                ..._knownDevices.map((d) => ListTile(
                  leading: Icon(
                    d.mac == _preferredMac ? Icons.star : Icons.bluetooth,
                    color: d.mac == _preferredMac ? kGreen : kDim, size: 20),
                  title: Text(d.nickname,
                    style: const TextStyle(color: kText, fontSize: 14)),
                  subtitle: Text(d.mac,
                    style: TextStyle(color: kDim, fontSize: 10)),
                  trailing: IconButton(
                    icon: Icon(Icons.delete_outline, color: kRed, size: 20),
                    onPressed: () async {
                      await _removeDevice(d.mac);
                      setSS(() {});
                    }),
                  onTap: () {
                    setState(() => _preferredMac = d.mac);
                    SharedPreferences.getInstance().then(
                      (p) => p.setString('preferred_mac', d.mac));
                    Navigator.pop(ctx);
                  },
                )),
              const SizedBox(height: 8),
            ],
          ),
        ),
      ),
    );
  }

  void _showMenu() {
    showModalBottomSheet(
      context: context,
      backgroundColor: kCard,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(16))),
      builder: (ctx) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 12),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(width: 40, height: 4,
              margin: const EdgeInsets.only(bottom: 16),
              decoration: BoxDecoration(
                color: kBorder, borderRadius: BorderRadius.circular(2))),
            _menuItem(Icons.bluetooth_searching, 'Scan for InclinoCore', () {
              Navigator.pop(ctx); _manualScan();
            }),
            _menuItem(Icons.devices, 'Known devices', () {
              Navigator.pop(ctx); _showKnownDevices();
            }),
            _menuItem(Icons.label_outline, 'Set device nickname',
              _connected && _pinVerified
                ? () { Navigator.pop(ctx); _setNickname(); } : null),
            const Divider(color: Color(0xFF1E3A1E), indent: 16, endIndent: 16),
            _menuItem(Icons.tune, 'Calibrate',
              _connected && _pinVerified
                ? () { Navigator.pop(ctx); _confirmCalibrate(); } : null,
              color: (_connected && _pinVerified) ? kAmber : kDim),
            const SizedBox(height: 8),
          ],
        ),
      ),
    );
  }

  Widget _menuItem(IconData icon, String label, VoidCallback? onTap,
      {Color? color}) {
    final c = color ?? (onTap != null ? kText : kDim);
    return ListTile(
      leading: Icon(icon, color: c, size: 20),
      title: Text(label, style: TextStyle(color: c, fontSize: 14)),
      onTap: onTap,
    );
  }

  void _toggleWakeLock() async {
    if (_wakeLock) {
      await _wakelockChannel.invokeMethod('disable');
    } else {
      await _wakelockChannel.invokeMethod('enable');
    }
    setState(() => _wakeLock = !_wakeLock);
  }

  @override
  Widget build(BuildContext context) {
    final bool level = _pitch.abs() < 1.0 && _roll.abs() < 1.0;
    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(children: [
            _buildHeader(),
            const SizedBox(height: 16),
            _buildConnectionCard(),
            const SizedBox(height: 16),
            Expanded(child: _buildLevelCard(level)),
            const SizedBox(height: 16),
            _buildReadingsCard(level),
          ]),
        ),
      ),
    );
  }

  Widget _buildHeader() {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text('INCLINOCAR',
            style: TextStyle(fontSize: 22, fontWeight: FontWeight.w200,
              letterSpacing: 6, color: kGreen)),
          Text('ROOFTOP TENT LEVELING',
            style: TextStyle(fontSize: 9, letterSpacing: 2, color: kDim)),
          Text('v$kAppVersion',
            style: TextStyle(fontSize: 9, letterSpacing: 1,
              color: kDim.withOpacity(0.6))),
        ]),
        Row(children: [
          GestureDetector(
            onTap: _toggleWakeLock,
            child: Container(
              padding: const EdgeInsets.all(6),
              margin: const EdgeInsets.only(right: 8),
              decoration: BoxDecoration(
                color: _wakeLock ? kGreenDim.withOpacity(0.2) : kCard,
                border: Border.all(color: _wakeLock ? kGreen : kBorder),
                borderRadius: BorderRadius.circular(8)),
              child: Icon(
                _wakeLock ? Icons.screen_lock_portrait
                          : Icons.screen_lock_portrait_outlined,
                size: 16, color: _wakeLock ? kGreen : kDim))),
          GestureDetector(
            onTap: _showMenu,
            child: Container(
              padding: const EdgeInsets.all(6),
              decoration: BoxDecoration(
                color: kCard, border: Border.all(color: kBorder),
                borderRadius: BorderRadius.circular(8)),
              child: Icon(Icons.more_vert, size: 16, color: kDim))),
        ]),
      ],
    );
  }

  Widget _buildConnectionCard() {
    final savedDevice = _preferredMac != null
      ? _knownDevices.where((d) => d.mac == _preferredMac).firstOrNull
      : null;
    final displayName = _connected
      ? (_nickname.isNotEmpty ? _nickname : 'InclinoCore')
      : savedDevice?.nickname ?? '';
    final displayMac = _connected ? _connectedMac : _preferredMac ?? '';

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: _cardDecor(),
      child: Row(children: [
        Expanded(child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              _connected
                ? (_pinVerified ? displayName : 'Verifying PIN...')
                : (displayName.isNotEmpty ? displayName : 'No saved device'),
              style: TextStyle(color: kText, fontSize: 14)),
            if (displayMac.isNotEmpty)
              Text(displayMac, style: TextStyle(color: kDim, fontSize: 10)),
            if (!_connected && _preferredMac == null)
              Text('Menu → Scan to find InclinoCore',
                style: TextStyle(color: kDim, fontSize: 10)),
          ],
        )),
        const SizedBox(width: 12),
        _scanning
          ? SizedBox(width: 20, height: 20,
              child: CircularProgressIndicator(strokeWidth: 2, color: kGreen))
          : _preferredMac != null && !_connected
            ? TextButton(
                style: TextButton.styleFrom(
                  foregroundColor: kGreen,
                  side: BorderSide(color: kGreen),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(6)),
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8)),
                onPressed: _autoConnect,
                child: const Text('Connect', style: TextStyle(fontSize: 13)))
            : _connected
              ? TextButton(
                  style: TextButton.styleFrom(
                    foregroundColor: kRed,
                    side: BorderSide(color: kRed),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(6)),
                    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8)),
                  onPressed: _disconnect,
                  child: const Text('Disconnect', style: TextStyle(fontSize: 13)))
              : const SizedBox.shrink(),
      ]),
    );
  }

  Widget _buildLevelCard(bool level) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: _cardDecor(),
      child: Column(children: [
        Text('LEVEL INDICATOR',
          style: TextStyle(fontSize: 10, letterSpacing: 2, color: kDim)),
        const SizedBox(height: 12),
        Expanded(child: Center(
          child: BubbleLevel(pitch: -_pitch, roll: -_roll, level: level))),
        const SizedBox(height: 12),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          decoration: BoxDecoration(
            color: level ? kGreenDim.withOpacity(0.3) : kCard,
            border: Border.all(color: level ? kGreen : kAmber),
            borderRadius: BorderRadius.circular(20)),
          child: Text(level ? '★  LEVEL  ★' : 'Adjust vehicle',
            style: TextStyle(
              color: level ? kGreen : kAmber,
              fontSize: 13, letterSpacing: 2)),
        ),
      ]),
    );
  }

  Widget _buildReadingsCard(bool level) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
      decoration: _cardDecor(),
      child: Row(children: [
        Expanded(child: _buildReading('PITCH', _pitch)),
        Container(width: 1, height: 50, color: kBorder),
        Expanded(child: _buildReading('ROLL', _roll)),
      ]),
    );
  }

  Widget _buildReading(String label, double value) {
    final bool ok = value.abs() < 1.0;
    return Column(children: [
      Text(label, style: TextStyle(fontSize: 10, letterSpacing: 2, color: kDim)),
      const SizedBox(height: 4),
      Text('${value >= 0 ? '+' : ''}${value.toStringAsFixed(1)}°',
        style: TextStyle(
          fontSize: 32, fontWeight: FontWeight.w300,
          color: ok ? kGreen : (value.abs() < 3.0 ? kAmber : kRed),
          letterSpacing: 1)),
    ]);
  }

  BoxDecoration _cardDecor() => BoxDecoration(
    color: kCard, border: Border.all(color: kBorder),
    borderRadius: BorderRadius.circular(12));
}

// ─── Bubble Level ─────────────────────────────────────────────
class BubbleLevel extends StatelessWidget {
  final double pitch, roll;
  final bool   level;
  const BubbleLevel({super.key, required this.pitch, required this.roll, required this.level});

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, constraints) {
      final size   = min(constraints.maxWidth, constraints.maxHeight);
      final radius = size / 2;
      final maxOff = radius * 0.60;
      final dx = (roll  / 15.0).clamp(-1.0, 1.0) * maxOff;
      final dy = (pitch / 15.0).clamp(-1.0, 1.0) * maxOff;
      return SizedBox(width: size, height: size,
        child: CustomPaint(
          painter: _BubblePainter(
            dx: dx, dy: dy, radius: radius,
            pitch: pitch, roll: roll,
            bubbleColor: level ? kGreen : kAmber, level: level)));
    });
  }
}

class _BubblePainter extends CustomPainter {
  final double dx, dy, radius, pitch, roll;
  final Color  bubbleColor;
  final bool   level;
  _BubblePainter({required this.dx, required this.dy, required this.radius,
    required this.pitch, required this.roll,
    required this.bubbleColor, required this.level});

  void _drawArrow(Canvas canvas, Offset tip, Offset b1, Offset b2, Color color) {
    canvas.drawPath(Path()..moveTo(tip.dx,tip.dy)..lineTo(b1.dx,b1.dy)
      ..lineTo(b2.dx,b2.dy)..close(), Paint()..color = color);
  }

  void _drawCar(Canvas canvas, double cx, double cy, double r) {
    final p = Paint()..color = kGreen.withOpacity(0.25)
      ..style = PaintingStyle.stroke..strokeWidth = 1.5;
    final carW = r*0.28; final carH = r*0.72;
    canvas.drawRRect(RRect.fromRectAndRadius(
      Rect.fromCenter(center: Offset(cx,cy), width: carW*2, height: carH*2),
      Radius.circular(carW*0.7)), p);
    canvas.drawRRect(RRect.fromRectAndRadius(
      Rect.fromCenter(center: Offset(cx,cy-carH*0.52), width: carW*1.5, height: carH*0.28),
      Radius.circular(4)),
      Paint()..color = kGreen.withOpacity(0.15));
    canvas.drawRRect(RRect.fromRectAndRadius(
      Rect.fromCenter(center: Offset(cx,cy-carH*0.52), width: carW*1.5, height: carH*0.28),
      Radius.circular(4)), p);
    canvas.drawRRect(RRect.fromRectAndRadius(
      Rect.fromCenter(center: Offset(cx,cy+carH*0.44), width: carW*1.3, height: carH*0.2),
      Radius.circular(3)), p);
    for (final pos in [
      Offset(cx-carW*1.25, cy-carH*0.5), Offset(cx+carW*1.25, cy-carH*0.5),
      Offset(cx-carW*1.25, cy+carH*0.4), Offset(cx+carW*1.25, cy+carH*0.4)]) {
      canvas.drawRRect(RRect.fromRectAndRadius(
        Rect.fromCenter(center: pos, width: carW*0.45, height: carH*0.22),
        Radius.circular(3)),
        Paint()..color = kGreen.withOpacity(0.3)..style = PaintingStyle.fill);
    }
  }

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width/2; final cy = size.height/2;
    canvas.drawCircle(Offset(cx,cy), radius-2,
      Paint()..style=PaintingStyle.stroke..color=kBorder..strokeWidth=1.5);
    canvas.drawCircle(Offset(cx,cy), radius*0.12,
      Paint()..style=PaintingStyle.stroke..color=kGreen.withOpacity(0.4)..strokeWidth=1);
    canvas.drawCircle(Offset(cx,cy), radius*0.28,
      Paint()..style=PaintingStyle.stroke..color=kBorder..strokeWidth=1);
    final hair = Paint()..color=kBorder..strokeWidth=1;
    canvas.drawLine(Offset(cx-radius+8,cy), Offset(cx+radius-8,cy), hair);
    canvas.drawLine(Offset(cx,cy-radius+8), Offset(cx,cy+radius-8), hair);
    final tick = Paint()..color=kDim..strokeWidth=1.5;
    for (int i=0;i<4;i++){
      final a=i*pi/2;
      canvas.drawLine(Offset(cx+(radius-10)*cos(a),cy+(radius-10)*sin(a)),
        Offset(cx+(radius-2)*cos(a),cy+(radius-2)*sin(a)), tick);
    }
    _drawCar(canvas, cx, cy, radius);
    final tp = TextPainter(
      text: TextSpan(text: 'FRONT',
        style: TextStyle(color: kDim.withOpacity(0.7), fontSize: 9, letterSpacing: 1)),
      textDirection: TextDirection.ltr)..layout();
    tp.paint(canvas, Offset(cx-tp.width/2, cy-radius+4));

    final ar=radius+14; final as2=8.0;
    _drawArrow(canvas, Offset(cx,cy-ar),
      Offset(cx-as2,cy-ar+as2*1.4), Offset(cx+as2,cy-ar+as2*1.4),
      pitch >  1.0 ? kAmber : kBorder);
    _drawArrow(canvas, Offset(cx,cy+ar),
      Offset(cx-as2,cy+ar-as2*1.4), Offset(cx+as2,cy+ar-as2*1.4),
      pitch < -1.0 ? kAmber : kBorder);
    _drawArrow(canvas, Offset(cx+ar,cy),
      Offset(cx+ar-as2*1.4,cy-as2), Offset(cx+ar-as2*1.4,cy+as2),
      roll  < -1.0 ? kAmber : kBorder);
    _drawArrow(canvas, Offset(cx-ar,cy),
      Offset(cx-ar+as2*1.4,cy-as2), Offset(cx-ar+as2*1.4,cy+as2),
      roll  >  1.0 ? kAmber : kBorder);

    canvas.drawCircle(Offset(cx+dx+1,cy+dy+1), radius*0.16,
      Paint()..color=Colors.black.withOpacity(0.3));
    canvas.drawCircle(Offset(cx+dx,cy+dy), radius*0.16,
      Paint()..shader=RadialGradient(
        colors:[bubbleColor.withOpacity(0.9),bubbleColor.withOpacity(0.5)],
      ).createShader(Rect.fromCircle(center:Offset(cx+dx,cy+dy),radius:radius*0.16)));
    canvas.drawCircle(Offset(cx+dx,cy+dy), radius*0.16,
      Paint()..style=PaintingStyle.stroke..color=bubbleColor..strokeWidth=1.5);
    canvas.drawCircle(Offset(cx+dx-radius*0.05,cy+dy-radius*0.05), radius*0.055,
      Paint()..color=Colors.white.withOpacity(0.35));
  }

  @override
  bool shouldRepaint(_BubblePainter o) =>
    dx!=o.dx||dy!=o.dy||level!=o.level||pitch!=o.pitch||roll!=o.roll;
}
