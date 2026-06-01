import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

// Nordic UART Service
const String NUS_SERVICE     = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const String NUS_TX          = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // device→phone
const MethodChannel _wakelockChannel = MethodChannel('com.inclinocar/wakelock');

const String NUS_RX          = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // phone→device

// ─── Theme ───────────────────────────────────────────────────
const Color kBg      = Color(0xFF0D1A0D);
const Color kCard    = Color(0xFF111E11);
const Color kGreen   = Color(0xFF4CAF50);
const Color kGreenDim= Color(0xFF2E7D32);
const Color kText    = Color(0xFFC8E6C8);
const Color kDim     = Color(0xFF5A8A5A);
const Color kBorder  = Color(0xFF1E3A1E);
const Color kAmber   = Color(0xFFFFB300);
const Color kRed     = Color(0xFFEF5350);

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
  void initState() {
    super.initState();
    _wakelockChannel.invokeMethod('enable');  // Keep screen on by default
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
    return MaterialApp(
      title: 'InclinoCar',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: kBg,
        colorScheme: const ColorScheme.dark(
          primary: kGreen,
          surface: kCard,
        ),
      ),
      home: const HomePage(),
    );
  }
}

// ─── Home Page ───────────────────────────────────────────────
class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  // BLE state
  BluetoothDevice?         _device;
  BluetoothCharacteristic? _txChar;
  BluetoothCharacteristic? _rxChar;
  StreamSubscription?      _scanSub;
  StreamSubscription?      _dataSub;
  StreamSubscription?      _connSub;

  bool   _scanning    = false;
  bool   _connected   = false;
  bool   _wakeLock    = true;
  String _status      = 'Not connected';

  // Inclinometer data
  double _pitch = 0.0;
  double _roll  = 0.0;

  // ── BLE ────────────────────────────────────────────────────
  Future<void> _requestPermissions() async {
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();
  }

  Future<void> _startScan() async {
    await _requestPermissions();
    setState(() { _scanning = true; _status = 'Scanning...'; });

    await FlutterBluePlus.startScan(
      withNames: ['InclinoCar'],
      timeout: const Duration(seconds: 10),
    );

    _scanSub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        if (r.device.platformName == 'InclinoCar') {
          FlutterBluePlus.stopScan();
          _connect(r.device);
          break;
        }
      }
    });

    FlutterBluePlus.isScanning.listen((scanning) {
      if (!scanning && mounted) {
        setState(() { _scanning = false; });
        if (!_connected) setState(() => _status = 'Device not found');
      }
    });
  }

  Future<void> _connect(BluetoothDevice device) async {
    setState(() { _status = 'Connecting...'; });
    _device = device;

    try {
      await device.connect(timeout: const Duration(seconds: 10));

      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          setState(() {
            _connected = false;
            _status    = 'Disconnected';
            _pitch     = 0.0;
            _roll      = 0.0;
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

      setState(() {
        _connected = true;
        _status    = 'Connected';
      });
    } catch (e) {
      setState(() { _status = 'Connection failed'; });
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
    });
  }

  void _onData(List<int> data) {
    final str = utf8.decode(data).trim();
    try {
      final json = jsonDecode(str) as Map<String, dynamic>;
      if (mounted) {
        setState(() {
          _pitch = (json['p'] as num).toDouble();
          _roll  = (json['r'] as num).toDouble();
        });
      }
    } catch (_) {}
  }

  Future<void> _sendCalibrate() async {
    if (_rxChar == null) return;
    try {
      // Try with response first, fall back to without response
      await _rxChar!.write(utf8.encode('CAL\n'), withoutResponse: false);
    } catch (_) {
      await _rxChar!.write(utf8.encode('CAL\n'), withoutResponse: true);
    }
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Calibration triggered — keep device still'),
          backgroundColor: kGreenDim,
          duration: Duration(seconds: 2),
        ),
      );
    }
  }

  // ── Build ───────────────────────────────────────────────────
  @override
  void initState() {
    super.initState();
    _wakelockChannel.invokeMethod('enable');  // Keep screen on by default
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
          child: Column(
            children: [
              _buildHeader(),
              const SizedBox(height: 16),
              _buildConnectionCard(),
              const SizedBox(height: 16),
              Expanded(child: _buildLevelCard(level)),
              const SizedBox(height: 16),
              _buildReadingsCard(level),
              const SizedBox(height: 16),
              _buildCalibrationButton(),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildHeader() {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('INCLINOCAR',
              style: TextStyle(
                fontSize: 22,
                fontWeight: FontWeight.w200,
                letterSpacing: 6,
                color: kGreen,
              )),
            Text('ROOFTOP TENT LEVELING',
              style: TextStyle(fontSize: 9, letterSpacing: 2, color: kDim)),
          ],
        ),
        Row(
          children: [
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
                  _wakeLock ? Icons.screen_lock_portrait : Icons.screen_lock_portrait_outlined,
                  size: 16,
                  color: _wakeLock ? kGreen : kDim,
                ),
              ),
            ),
            // Connection status
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
              decoration: BoxDecoration(
                color: _connected ? kGreenDim.withOpacity(0.3) : kCard,
                border: Border.all(color: _connected ? kGreen : kBorder),
                borderRadius: BorderRadius.circular(20),
              ),
              child: Row(children: [
                Container(
                  width: 8, height: 8,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: _connected ? kGreen : kDim,
                  ),
                ),
                const SizedBox(width: 6),
                Text(_connected ? 'Connected' : _status,
                  style: TextStyle(
                    fontSize: 11,
                    color: _connected ? kGreen : kDim,
                  )),
              ]),
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildConnectionCard() {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: _cardDecor(),
      child: Row(
        children: [
          Expanded(
            child: Text(
              _connected
                ? _device?.platformName ?? 'InclinoCar'
                : 'Tap to connect to InclinoCar',
              style: TextStyle(color: kText, fontSize: 14),
            ),
          ),
          const SizedBox(width: 12),
          _scanning
            ? SizedBox(
                width: 20, height: 20,
                child: CircularProgressIndicator(
                  strokeWidth: 2, color: kGreen))
            : TextButton(
                style: TextButton.styleFrom(
                  foregroundColor: _connected ? kRed : kGreen,
                  side: BorderSide(color: _connected ? kRed : kGreen),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(6)),
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                ),
                onPressed: _connected ? _disconnect : _startScan,
                child: Text(_connected ? 'Disconnect' : 'Connect',
                  style: const TextStyle(fontSize: 13)),
              ),
        ],
      ),
    );
  }

  Widget _buildLevelCard(bool level) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: _cardDecor(),
      child: Column(
        children: [
          Text('LEVEL INDICATOR',
            style: TextStyle(fontSize: 10, letterSpacing: 2, color: kDim)),
          const SizedBox(height: 12),
          Expanded(
            child: Center(
              child: BubbleLevel(pitch: _pitch, roll: _roll, level: level),
            ),
          ),
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
                fontSize: 13,
                letterSpacing: 2,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildReadingsCard(bool level) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
      decoration: _cardDecor(),
      child: Row(
        children: [
          Expanded(child: _buildReading('PITCH', _pitch)),
          Container(width: 1, height: 50, color: kBorder),
          Expanded(child: _buildReading('ROLL', _roll)),
        ],
      ),
    );
  }

  Widget _buildReading(String label, double value) {
    final bool ok = value.abs() < 1.0;
    return Column(
      children: [
        Text(label,
          style: TextStyle(fontSize: 10, letterSpacing: 2, color: kDim)),
        const SizedBox(height: 4),
        Text(
          '${value >= 0 ? '+' : ''}${value.toStringAsFixed(1)}°',
          style: TextStyle(
            fontSize: 32,
            fontWeight: FontWeight.w300,
            color: ok ? kGreen : (value.abs() < 3.0 ? kAmber : kRed),
            letterSpacing: 1,
          ),
        ),
      ],
    );
  }

  Widget _buildCalibrationButton() {
    return SizedBox(
      width: double.infinity,
      child: OutlinedButton(
        style: OutlinedButton.styleFrom(
          foregroundColor: _connected ? kText : kDim,
          side: BorderSide(color: _connected ? kBorder : kBorder.withOpacity(0.5)),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
          padding: const EdgeInsets.symmetric(vertical: 14),
        ),
        onPressed: _connected ? _sendCalibrate : null,
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(Icons.tune, size: 16, color: _connected ? kGreen : kDim),
            const SizedBox(width: 8),
            Text('Calibrate',
              style: TextStyle(
                fontSize: 14,
                letterSpacing: 1,
                color: _connected ? kText : kDim,
              )),
          ],
        ),
      ),
    );
  }

  BoxDecoration _cardDecor() => BoxDecoration(
    color: kCard,
    border: Border.all(color: kBorder),
    borderRadius: BorderRadius.circular(12),
  );

  @override
  void dispose() {
    _scanSub?.cancel();
    _dataSub?.cancel();
    _connSub?.cancel();
    _wakelockChannel.invokeMethod('disable');
    super.dispose();
  }
}

// ─── Bubble Level Widget ─────────────────────────────────────
class BubbleLevel extends StatelessWidget {
  final double pitch;
  final double roll;
  final bool   level;

  const BubbleLevel({
    super.key,
    required this.pitch,
    required this.roll,
    required this.level,
  });

  @override
  void initState() {
    super.initState();
    _wakelockChannel.invokeMethod('enable');  // Keep screen on by default
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
    return LayoutBuilder(builder: (context, constraints) {
      final size   = min(constraints.maxWidth, constraints.maxHeight);
      final radius = size / 2;
      final maxOff = radius * 0.65;

      // Map pitch/roll to pixel offset — clamp at maxOff
      final dx = (roll  / 15.0).clamp(-1.0, 1.0) * maxOff;
      final dy = (pitch / 15.0).clamp(-1.0, 1.0) * maxOff;

      final bubbleColor = level ? kGreen : kAmber;

      return SizedBox(
        width:  size,
        height: size,
        child: CustomPaint(
          painter: _BubblePainter(
            dx: dx,
            dy: dy,
            radius: radius,
            bubbleColor: bubbleColor,
            level: level,
          ),
        ),
      );
    });
  }
}

class _BubblePainter extends CustomPainter {
  final double dx, dy, radius;
  final Color  bubbleColor;
  final bool   level;

  _BubblePainter({
    required this.dx,
    required this.dy,
    required this.radius,
    required this.bubbleColor,
    required this.level,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width  / 2;
    final cy = size.height / 2;

    // Outer circle
    canvas.drawCircle(
      Offset(cx, cy), radius - 2,
      Paint()
        ..style = PaintingStyle.stroke
        ..color = kBorder
        ..strokeWidth = 1.5,
    );

    // Centre target rings
    canvas.drawCircle(
      Offset(cx, cy), radius * 0.12,
      Paint()
        ..style = PaintingStyle.stroke
        ..color = kGreen.withOpacity(0.4)
        ..strokeWidth = 1,
    );
    canvas.drawCircle(
      Offset(cx, cy), radius * 0.28,
      Paint()
        ..style = PaintingStyle.stroke
        ..color = kBorder
        ..strokeWidth = 1,
    );

    // Crosshair
    final hairPaint = Paint()
      ..color = kBorder
      ..strokeWidth = 1;
    canvas.drawLine(Offset(cx - radius + 8, cy), Offset(cx + radius - 8, cy), hairPaint);
    canvas.drawLine(Offset(cx, cy - radius + 8), Offset(cx, cy + radius - 8), hairPaint);

    // Cardinal tick marks
    final tickPaint = Paint()..color = kDim..strokeWidth = 1.5;
    for (int i = 0; i < 4; i++) {
      final angle = i * pi / 2;
      final x1 = cx + (radius - 10) * cos(angle);
      final y1 = cy + (radius - 10) * sin(angle);
      final x2 = cx + (radius - 2)  * cos(angle);
      final y2 = cy + (radius - 2)  * sin(angle);
      canvas.drawLine(Offset(x1, y1), Offset(x2, y2), tickPaint);
    }

    // Bubble shadow
    canvas.drawCircle(
      Offset(cx + dx + 1, cy + dy + 1),
      radius * 0.18,
      Paint()..color = Colors.black.withOpacity(0.3),
    );

    // Bubble fill
    canvas.drawCircle(
      Offset(cx + dx, cy + dy),
      radius * 0.18,
      Paint()
        ..shader = RadialGradient(
          colors: [
            bubbleColor.withOpacity(0.9),
            bubbleColor.withOpacity(0.5),
          ],
        ).createShader(Rect.fromCircle(
          center: Offset(cx + dx, cy + dy),
          radius: radius * 0.18,
        )),
    );

    // Bubble outline
    canvas.drawCircle(
      Offset(cx + dx, cy + dy),
      radius * 0.18,
      Paint()
        ..style = PaintingStyle.stroke
        ..color = bubbleColor
        ..strokeWidth = 1.5,
    );

    // Highlight on bubble
    canvas.drawCircle(
      Offset(cx + dx - radius * 0.05, cy + dy - radius * 0.05),
      radius * 0.06,
      Paint()..color = Colors.white.withOpacity(0.3),
    );
  }

  @override
  bool shouldRepaint(_BubblePainter old) =>
    dx != old.dx || dy != old.dy || level != old.level;
}
