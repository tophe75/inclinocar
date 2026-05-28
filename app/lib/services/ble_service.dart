import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/ble_uuids.dart';
import '../models/inclination_data.dart';

enum BleStatus { idle, scanning, connecting, connected, disconnected, error }

class BleService extends ChangeNotifier {
  BleStatus _status = BleStatus.idle;
  BluetoothDevice? _device;
  InclinationData _data = InclinationData(pitch: 0, roll: 0);
  String _errorMessage = '';

  // Internal state for partial updates
  double _pitch    = 0;
  double _roll     = 0;
  double? _satPitch;
  double? _satRoll;

  final List<StreamSubscription> _subscriptions = [];

  BleStatus get status        => _status;
  InclinationData get data    => _data;
  String get errorMessage     => _errorMessage;
  bool get isConnected        => _status == BleStatus.connected;

  // ─── Scan & Connect ───────────────────────────────────────────
  Future<void> startScan() async {
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

    // If still scanning after timeout = not found
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
          if ([BleUuids.pitch, BleUuids.roll, BleUuids.satPitch, BleUuids.satRoll]
              .contains(uuid)) {
            await char.setNotifyValue(true);
            final sub = char.lastValueStream.listen((value) {
              if (value.length >= 4) _onCharacteristicValue(uuid, value);
            });
            _subscriptions.add(sub);
          }
        }
      }
    }
  }

  void _onCharacteristicValue(String uuid, List<int> value) {
    final v = InclinationData.parseFloat(value);
    switch (uuid) {
      case BleUuids.pitch:    _pitch    = v; break;
      case BleUuids.roll:     _roll     = v; break;
      case BleUuids.satPitch: _satPitch = v; break;
      case BleUuids.satRoll:  _satRoll  = v; break;
    }
    _data = InclinationData(
      pitch:    _pitch,
      roll:     _roll,
      satPitch: _satPitch,
      satRoll:  _satRoll,
    );
    notifyListeners();
  }

  Future<void> disconnect() async {
    await _device?.disconnect();
    _cancelSubscriptions();
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
