import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/ble_uuids.dart';
import '../models/inclination_data.dart';

enum BleStatus { idle, scanning, connecting, connected, disconnected, error }

class BleService extends ChangeNotifier {
  BleStatus _status        = BleStatus.idle;
  BluetoothDevice? _device;
  InclinationData _data    = InclinationData(pitch: 0, roll: 0);
  String _errorMessage     = '';
  bool _isCalibrated       = false;
  bool _isCalibrating      = false;

  double _pitch = 0;
  double _roll  = 0;

  BluetoothCharacteristic? _calibChar;
  final List<StreamSubscription> _subscriptions = [];

  BleStatus get status          => _status;
  InclinationData get data      => _data;
  String get errorMessage       => _errorMessage;
  bool get isConnected          => _status == BleStatus.connected;
  bool get isCalibrated         => _isCalibrated;
  bool get isCalibrating        => _isCalibrating;

  // ─── Scan & Connect ───────────────────────────────────────────
  Future<void> startScan() async {
    _errorMessage = '';
    _setStatus(BleStatus.scanning);
    await FlutterBluePlus.stopScan();

    final sub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        if (r.device.platformName == AppConstants.deviceName) {
          FlutterBluePlus.stopScan();
          _connect(r.device);
          return;
        }
      }
    });
    _subscriptions.add(sub);

    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    if (_status == BleStatus.scanning) {
      _setStatus(BleStatus.idle);
      _errorMessage = 'InclinoCar device not found. Is it powered on?';
      notifyListeners();
    }
  }

  Future<void> _connect(BluetoothDevice device) async {
    _setStatus(BleStatus.connecting);
    _device = device;
    try {
      await device.connect(autoConnect: false);
      _setStatus(BleStatus.connected);

      final sub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _isCalibrated = false;
          _setStatus(BleStatus.disconnected);
        }
      });
      _subscriptions.add(sub);
      await _discoverServices(device);
    } catch (e) {
      _errorMessage = 'Connection failed: $e';
      _setStatus(BleStatus.error);
    }
  }

  Future<void> _discoverServices(BluetoothDevice device) async {
    final services = await device.discoverServices();
    for (final service in services) {
      if (service.uuid.toString() == BleUuids.service) {
        for (final char in service.characteristics) {
          final uuid = char.uuid.toString();
          if (uuid == BleUuids.pitch || uuid == BleUuids.roll) {
            await char.setNotifyValue(true);
            final sub = char.lastValueStream.listen((value) {
              if (value.length >= 4) _onCharValue(uuid, value);
            });
            _subscriptions.add(sub);
          }
          if (uuid == BleUuids.calibrate) {
            _calibChar = char;
          }
          if (uuid == BleUuids.status) {
            await char.setNotifyValue(true);
            final sub = char.lastValueStream.listen((value) {
              if (value.isNotEmpty && value[0] == 0x01) {
                _isCalibrated  = true;
                _isCalibrating = false;
                notifyListeners();
              }
            });
            _subscriptions.add(sub);
          }
        }
      }
    }
    // Assume calibrated after connect (core calibrates on boot)
    _isCalibrated = true;
    notifyListeners();
  }

  void _onCharValue(String uuid, List<int> value) {
    final v = InclinationData.parseFloat(value);
    if (uuid == BleUuids.pitch) _pitch = v;
    if (uuid == BleUuids.roll)  _roll  = v;
    _data = InclinationData(pitch: _pitch, roll: _roll);
    notifyListeners();
  }

  // ─── Calibration ──────────────────────────────────────────────
  Future<void> resetCalibration() async {
    if (_calibChar == null) return;
    _isCalibrating = true;
    _isCalibrated  = false;
    notifyListeners();
    try {
      await _calibChar!.write([0x01], withoutResponse: false);
    } catch (e) {
      _isCalibrating = false;
      notifyListeners();
    }
  }

  Future<void> disconnect() async {
    await _device?.disconnect();
    _cancelSubscriptions();
    _isCalibrated = false;
    _setStatus(BleStatus.idle);
  }

  void _setStatus(BleStatus s) {
    _status = s;
    notifyListeners();
  }

  void _cancelSubscriptions() {
    for (final s in _subscriptions) { s.cancel(); }
    _subscriptions.clear();
  }

  @override
  void dispose() {
    _cancelSubscriptions();
    super.dispose();
  }
}
