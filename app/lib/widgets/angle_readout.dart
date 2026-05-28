import 'package:flutter/material.dart';
import '../models/inclination_data.dart';

class AngleReadout extends StatelessWidget {
  final InclinationData data;

  const AngleReadout({super.key, required this.data});

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceEvenly,
      children: [
        _AngleTile(
          label: 'PITCH',
          value: data.pitch,
          isLevel: data.isPitchLevel,
          direction: data.pitch > 0 ? 'Front up' : 'Front down',
        ),
        _divider(),
        _AngleTile(
          label: 'ROLL',
          value: data.roll,
          isLevel: data.isRollLevel,
          direction: data.roll > 0 ? 'Right up' : 'Left up',
        ),
      ],
    );
  }

  Widget _divider() => Container(
    width: 1, height: 60,
    color: const Color(0xFF2A3A2A),
  );
}

class _AngleTile extends StatelessWidget {
  final String label;
  final double value;
  final bool isLevel;
  final String direction;

  const _AngleTile({
    required this.label,
    required this.value,
    required this.isLevel,
    required this.direction,
  });

  Color get _color {
    if (isLevel) return const Color(0xFF4CAF50);
    if (value.abs() < 2.0) return const Color(0xFFFF9800);
    return const Color(0xFFF44336);
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Text(label,
          style: const TextStyle(
            color: Color(0xFF5A8A5A),
            fontSize: 11,
            letterSpacing: 2,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 4),
        Text(
          '${value >= 0 ? '+' : ''}${value.toStringAsFixed(1)}°',
          style: TextStyle(
            color: _color,
            fontSize: 32,
            fontWeight: FontWeight.w300,
            fontFamily: 'monospace',
          ),
        ),
        const SizedBox(height: 2),
        Text(
          isLevel ? '✓ Level' : direction,
          style: TextStyle(
            color: _color.withOpacity(0.7),
            fontSize: 11,
          ),
        ),
      ],
    );
  }
}
