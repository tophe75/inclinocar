import 'dart:typed_data';

class InclinationData {
  final double pitch;
  final double roll;
  final double? satPitch;
  final double? satRoll;
  final DateTime timestamp;

  InclinationData({
    required this.pitch,
    required this.roll,
    this.satPitch,
    this.satRoll,
    DateTime? timestamp,
  }) : timestamp = timestamp ?? DateTime.now();

  bool get isPitchLevel => pitch.abs() <= 0.5;
  bool get isRollLevel  => roll.abs()  <= 0.5;
  bool get isLevel      => isPitchLevel && isRollLevel;

  /// Parse a 4-byte IEEE 754 float from BLE characteristic value
  static double parseFloat(List<int> bytes) {
    final data = Uint8List.fromList(bytes);
    return ByteData.sublistView(data).getFloat32(0, Endian.little);
  }

  InclinationData copyWith({
    double? pitch,
    double? roll,
    double? satPitch,
    double? satRoll,
  }) {
    return InclinationData(
      pitch:    pitch    ?? this.pitch,
      roll:     roll     ?? this.roll,
      satPitch: satPitch ?? this.satPitch,
      satRoll:  satRoll  ?? this.satRoll,
    );
  }

  @override
  String toString() =>
      'InclinationData(pitch: ${pitch.toStringAsFixed(2)}, roll: ${roll.toStringAsFixed(2)})';
}
