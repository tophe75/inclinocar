import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

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

const String kAppVersion = '0.0.13';

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
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'InclinoCar',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: kBg,
        colorScheme: const ColorScheme.dark(primary: kGreen, surface: kCard),
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});
  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  // BLE
  BluetoothDevice?         _device;
  BluetoothCharacteristic? _txChar;
  BluetoothCharacteristic? _rxChar;
  StreamSubscription?      _scanSub;
  StreamSubscription?      _dataSub;
  StreamSubscription?      _connSub;
  String?                  _knownMac;  // last connected MAC for auto-reconnect

  bool   _scanning  = false;
  bool   _connected = false;
  bool   _wakeLock  = true;
  String _status    = 'Not connected';
  String _nickname  = 'InclinoCore';

  double _pitch = 0.0;
  double _roll  = 0.0;

  @override
  void initState() {
    super.initState();
    _wakelockChannel.invokeMethod('enable');
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    _dataSub?.cancel();
    _connSub?.cancel();
    _wakelockChannel.invokeMethod('disable');
    super.dispose();
  }

  void _toggleWakeLock() async {
    if (_wakeLock) {
      await _wakelockChannel.invokeMethod('disable');
    } else {
      await _wakelockChannel.invokeMethod('enable');
    }
    setState(() => _wakeLock = !_wakeLock);
  }

  Future<void> _requestPermissions() async {
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();
  }

  // Auto-connect: tries known MAC first, falls back to name scan
  Future<void> _autoConnect() async {
    await _requestPermissions();
    setState(() { _scanning = true; _status = 'Scanning...'; });

    await FlutterBluePlus.startScan(
      withServices: [Guid(NUS_SERVICE)],
      timeout: const Duration(seconds: 10),
    );

    _scanSub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        // Prefer known MAC, accept any device advertising NUS service
        final macMatch = _knownMac != null &&
            r.device.remoteId.toString() == _knownMac;
        final serviceMatch = r.advertisementData.serviceUuids
            .any((u) => u.toString().toLowerCase() == NUS_SERVICE);
        if (macMatch || serviceMatch) {
          FlutterBluePlus.stopScan();
          _connect(r.device);
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

  // Manual scan — shows all InclinoCore devices to pick from
  Future<void> _manualScan() async {
    await _requestPermissions();
    final found = <ScanResult>[];

    setState(() => _scanning = true);

    await FlutterBluePlus.startScan(
      withServices: [Guid(NUS_SERVICE)],
      timeout: const Duration(seconds: 8),
    );

    await for (final results in FlutterBluePlus.scanResults) {
      for (final r in results) {
        if (!found.any((e) => e.device.remoteId == r.device.remoteId)) {
          found.add(r);
        }
      }
    }

    setState(() => _scanning = false);
    if (!mounted) return;

    if (found.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
        content: Text('No InclinoCore devices found'),
        backgroundColor: kRed,
      ));
      return;
    }

    // Show picker
    final picked = await showDialog<BluetoothDevice>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: kCard,
        title: const Text('Select InclinoCore',
          style: TextStyle(color: kText, fontSize: 16)),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: found.map((r) => ListTile(
            title: Text(r.device.platformName,
              style: const TextStyle(color: kText)),
            subtitle: Text(r.device.remoteId.toString(),
              style: TextStyle(color: kDim, fontSize: 11)),
            trailing: Text('${r.rssi} dBm',
              style: TextStyle(color: kDim, fontSize: 11)),
            onTap: () => Navigator.pop(ctx, r.device),
          )).toList(),
        ),
      ),
    );

    if (picked != null) {
      _knownMac = picked.remoteId.toString();
      _connect(picked);
    }
  }

  Future<void> _connect(BluetoothDevice device) async {
    setState(() => _status = 'Connecting...');
    _device = device;
    _knownMac = device.remoteId.toString();

    try {
      await device.connect(timeout: const Duration(seconds: 10));

      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          setState(() {
            _connected = false;
            _status    = 'Disconnected';
            _pitch     = 0.0;
            _roll      = 0.0;
            _nickname  = 'InclinoCore';
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

      setState(() { _connected = true; _status = 'Connected'; });
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
      _connected = false;
      _status    = 'Not connected';
      _pitch     = 0.0;
      _roll      = 0.0;
      _nickname  = 'InclinoCore';
    });
  }

  void _onData(List<int> data) {
    final str = utf8.decode(data).trim();
    try {
      final json = jsonDecode(str) as Map<String, dynamic>;
      if (mounted) setState(() {
        _pitch = (json['p'] as num).toDouble();
        _roll  = (json['r'] as num).toDouble();
        if (json['n'] != null) _nickname = json['n'] as String;
      });
    } catch (_) {}
  }

  Future<void> _sendCommand(String cmd) async {
    if (_rxChar == null) return;
    try {
      await _rxChar!.write(utf8.encode('$cmd\n'), withoutResponse: true);
    } catch (e) {
      debugPrint('BLE write error: $e');
    }
  }

  Future<void> _confirmCalibrate() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: kCard,
        title: const Text('Calibrate?',
          style: TextStyle(color: kText)),
        content: const Text(
          'Place the vehicle on flat level ground and keep it still.\n\nAre you sure you want to calibrate?',
          style: TextStyle(color: kDim, fontSize: 14)),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: Text('Cancel', style: TextStyle(color: kDim)),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: Text('Calibrate', style: TextStyle(color: kGreen)),
          ),
        ],
      ),
    );
    if (confirmed == true) {
      await _sendCommand('CAL');
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
        content: Text('Calibrating — keep device still for 2 seconds'),
        backgroundColor: kGreenDim,
        duration: Duration(seconds: 3),
      ));
    }
  }

  Future<void> _setNickname() async {
    final controller = TextEditingController(text: _nickname);
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: kCard,
        title: const Text('Device Nickname',
          style: TextStyle(color: kText)),
        content: TextField(
          controller: controller,
          maxLength: 20,
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
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: Text('Cancel', style: TextStyle(color: kDim)),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, controller.text.trim()),
            child: Text('Save', style: TextStyle(color: kGreen)),
          ),
        ],
      ),
    );
    if (result != null && result.isNotEmpty) {
      await _sendCommand('NICK:$result');
      setState(() => _nickname = result);
    }
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
            Container(
              width: 40, height: 4,
              margin: const EdgeInsets.only(bottom: 16),
              decoration: BoxDecoration(
                color: kBorder, borderRadius: BorderRadius.circular(2)),
            ),
            _menuItem(Icons.bluetooth_searching, 'Scan for InclinoCore', () {
              Navigator.pop(ctx);
              _manualScan();
            }),
            _menuItem(Icons.label_outline, 'Set device nickname',
              _connected ? () { Navigator.pop(ctx); _setNickname(); } : null),
            const Divider(color: Color(0xFF1E3A1E), indent: 16, endIndent: 16),
            _menuItem(Icons.tune, 'Calibrate',
              _connected ? () { Navigator.pop(ctx); _confirmCalibrate(); } : null,
              color: _connected ? kAmber : kDim),
            const SizedBox(height: 8),
          ],
        ),
      ),
    );
  }

  Widget _menuItem(IconData icon, String label, VoidCallback? onTap,
      {Color? color}) {
    final c = color ?? (_connected || label == 'Scan for InclinoCore' ? kText : kDim);
    return ListTile(
      leading: Icon(icon, color: c, size: 20),
      title: Text(label, style: TextStyle(color: c, fontSize: 14)),
      onTap: onTap,
    );
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
          // Wake lock toggle
          GestureDetector(
            onTap: _toggleWakeLock,
            child: Container(
              padding: const EdgeInsets.all(6),
              margin: const EdgeInsets.only(right: 8),
              decoration: BoxDecoration(
                color: _wakeLock ? kGreenDim.withOpacity(0.2) : kCard,
                border: Border.all(color: _wakeLock ? kGreen : kBorder),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Icon(
                _wakeLock ? Icons.screen_lock_portrait
                          : Icons.screen_lock_portrait_outlined,
                size: 16,
                color: _wakeLock ? kGreen : kDim,
              ),
            ),
          ),
          // Three-dot menu
          GestureDetector(
            onTap: _showMenu,
            child: Container(
              padding: const EdgeInsets.all(6),
              decoration: BoxDecoration(
                color: kCard,
                border: Border.all(color: kBorder),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Icon(Icons.more_vert, size: 16, color: kDim),
            ),
          ),
        ]),
      ],
    );
  }

  Widget _buildConnectionCard() {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: _cardDecor(),
      child: Row(children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                _connected ? _nickname : 'Tap Connect to find InclinoCore',
                style: TextStyle(color: kText, fontSize: 14),
              ),
              if (_connected)
                Text(_device?.remoteId.toString() ?? '',
                  style: TextStyle(color: kDim, fontSize: 10)),
            ],
          ),
        ),
        const SizedBox(width: 12),
        _scanning
          ? SizedBox(width: 20, height: 20,
              child: CircularProgressIndicator(strokeWidth: 2, color: kGreen))
          : TextButton(
              style: TextButton.styleFrom(
                foregroundColor: _connected ? kRed : kGreen,
                side: BorderSide(color: _connected ? kRed : kGreen),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(6)),
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              ),
              onPressed: _connected ? _disconnect : _autoConnect,
              child: Text(_connected ? 'Disconnect' : 'Connect',
                style: const TextStyle(fontSize: 13)),
            ),
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
          child: BubbleLevel(pitch: -_pitch, roll: -_roll, level: level),
        )),
        const SizedBox(height: 12),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          decoration: BoxDecoration(
            color: level ? kGreenDim.withOpacity(0.3) : kCard,
            border: Border.all(color: level ? kGreen : kAmber),
            borderRadius: BorderRadius.circular(20),
          ),
          child: Text(
            level ? '★  LEVEL  ★' : 'Adjust vehicle',
            style: TextStyle(
              color: level ? kGreen : kAmber,
              fontSize: 13, letterSpacing: 2),
          ),
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
      Text(label,
        style: TextStyle(fontSize: 10, letterSpacing: 2, color: kDim)),
      const SizedBox(height: 4),
      Text(
        '${value >= 0 ? '+' : ''}${value.toStringAsFixed(1)}°',
        style: TextStyle(
          fontSize: 32, fontWeight: FontWeight.w300,
          color: ok ? kGreen : (value.abs() < 3.0 ? kAmber : kRed),
          letterSpacing: 1,
        ),
      ),
    ]);
  }

  BoxDecoration _cardDecor() => BoxDecoration(
    color: kCard,
    border: Border.all(color: kBorder),
    borderRadius: BorderRadius.circular(12),
  );
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

  void _drawArrow(Canvas canvas, Offset tip, Offset base1, Offset base2, Color color) {
    final path = Path()..moveTo(tip.dx, tip.dy)
      ..lineTo(base1.dx, base1.dy)..lineTo(base2.dx, base2.dy)..close();
    canvas.drawPath(path, Paint()..color = color);
  }

  void _drawCar(Canvas canvas, double cx, double cy, double r) {
    final paint = Paint()
      ..color = kGreen.withOpacity(0.25)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5;

    final carW = r * 0.28;
    final carH = r * 0.72;

    // Body
    final body = RRect.fromRectAndRadius(
      Rect.fromCenter(center: Offset(cx, cy), width: carW * 2, height: carH * 2),
      Radius.circular(carW * 0.7));
    canvas.drawRRect(body, paint);

    // Windscreen
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromCenter(center: Offset(cx, cy - carH * 0.52),
          width: carW * 1.5, height: carH * 0.28),
        Radius.circular(4)),
      Paint()..color = kGreen.withOpacity(0.15)..style = PaintingStyle.fill);
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromCenter(center: Offset(cx, cy - carH * 0.52),
          width: carW * 1.5, height: carH * 0.28),
        Radius.circular(4)),
      paint);

    // Rear window
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromCenter(center: Offset(cx, cy + carH * 0.44),
          width: carW * 1.3, height: carH * 0.2),
        Radius.circular(3)),
      paint);

    // Wheels — all four
    for (final pos in [
      Offset(cx - carW * 1.25, cy - carH * 0.5),
      Offset(cx + carW * 1.25, cy - carH * 0.5),
      Offset(cx - carW * 1.25, cy + carH * 0.4),
      Offset(cx + carW * 1.25, cy + carH * 0.4),
    ]) {
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromCenter(center: pos, width: carW * 0.45, height: carH * 0.22),
          Radius.circular(3)),
        Paint()..color = kGreen.withOpacity(0.3)..style = PaintingStyle.fill);
    }
  }

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;

    // Outer circle
    canvas.drawCircle(Offset(cx, cy), radius - 2,
      Paint()..style = PaintingStyle.stroke..color = kBorder..strokeWidth = 1.5);
    // Inner rings
    canvas.drawCircle(Offset(cx, cy), radius * 0.12,
      Paint()..style = PaintingStyle.stroke..color = kGreen.withOpacity(0.4)..strokeWidth = 1);
    canvas.drawCircle(Offset(cx, cy), radius * 0.28,
      Paint()..style = PaintingStyle.stroke..color = kBorder..strokeWidth = 1);

    // Crosshair
    final hair = Paint()..color = kBorder..strokeWidth = 1;
    canvas.drawLine(Offset(cx - radius + 8, cy), Offset(cx + radius - 8, cy), hair);
    canvas.drawLine(Offset(cx, cy - radius + 8), Offset(cx, cy + radius - 8), hair);

    // Tick marks
    final tick = Paint()..color = kDim..strokeWidth = 1.5;
    for (int i = 0; i < 4; i++) {
      final a = i * pi / 2;
      canvas.drawLine(
        Offset(cx + (radius-10)*cos(a), cy + (radius-10)*sin(a)),
        Offset(cx + (radius-2) *cos(a), cy + (radius-2) *sin(a)), tick);
    }

    // Car silhouette
    _drawCar(canvas, cx, cy, radius);

    // FRONT label
    final textPainter = TextPainter(
      text: TextSpan(text: 'FRONT',
        style: TextStyle(color: kDim.withOpacity(0.7), fontSize: 9,
          fontFamily: 'monospace', letterSpacing: 1)),
      textDirection: TextDirection.ltr,
    )..layout();
    textPainter.paint(canvas,
      Offset(cx - textPainter.width / 2, cy - radius + 2));

    // Directional arrows — light up when off level
    final double threshold = 1.0;
    final arrowR  = radius + 14;
    final arrowSz = 8.0;

    // pitch > 0 = front high → lower front (arrow points down = rear)
    // pitch < 0 = front low → raise front (arrow points up = front)
    // roll  > 0 = right high → lower right
    // roll  < 0 = left high  → lower left

    final frontActive = pitch >  threshold;
    final rearActive  = pitch < -threshold;
    final rightActive = roll  > -threshold && roll < -threshold ? false : roll < -threshold;
    final leftActive  = roll  >  threshold;

    // Top arrow (raise front)
    final topColor = frontActive ? kAmber : kBorder;
    _drawArrow(canvas,
      Offset(cx, cy - arrowR),
      Offset(cx - arrowSz, cy - arrowR + arrowSz * 1.4),
      Offset(cx + arrowSz, cy - arrowR + arrowSz * 1.4),
      topColor);

    // Bottom arrow (raise rear / lower front)
    final botColor = rearActive ? kAmber : kBorder;
    _drawArrow(canvas,
      Offset(cx, cy + arrowR),
      Offset(cx - arrowSz, cy + arrowR - arrowSz * 1.4),
      Offset(cx + arrowSz, cy + arrowR - arrowSz * 1.4),
      botColor);

    // Right arrow
    final rightColor = rightActive ? kAmber : kBorder;
    _drawArrow(canvas,
      Offset(cx + arrowR, cy),
      Offset(cx + arrowR - arrowSz * 1.4, cy - arrowSz),
      Offset(cx + arrowR - arrowSz * 1.4, cy + arrowSz),
      rightColor);

    // Left arrow
    final leftColor = leftActive ? kAmber : kBorder;
    _drawArrow(canvas,
      Offset(cx - arrowR, cy),
      Offset(cx - arrowR + arrowSz * 1.4, cy - arrowSz),
      Offset(cx - arrowR + arrowSz * 1.4, cy + arrowSz),
      leftColor);

    // Bubble shadow
    canvas.drawCircle(Offset(cx+dx+1, cy+dy+1), radius*0.16,
      Paint()..color = Colors.black.withOpacity(0.3));
    // Bubble fill
    canvas.drawCircle(Offset(cx+dx, cy+dy), radius*0.16,
      Paint()..shader = RadialGradient(
        colors: [bubbleColor.withOpacity(0.9), bubbleColor.withOpacity(0.5)],
      ).createShader(Rect.fromCircle(center: Offset(cx+dx, cy+dy), radius: radius*0.16)));
    // Bubble outline
    canvas.drawCircle(Offset(cx+dx, cy+dy), radius*0.16,
      Paint()..style = PaintingStyle.stroke..color = bubbleColor..strokeWidth = 1.5);
    // Bubble glint
    canvas.drawCircle(
      Offset(cx+dx - radius*0.05, cy+dy - radius*0.05), radius*0.055,
      Paint()..color = Colors.white.withOpacity(0.35));
  }

  @override
  bool shouldRepaint(_BubblePainter old) =>
    dx != old.dx || dy != old.dy || level != old.level ||
    pitch != old.pitch || roll != old.roll;
}
