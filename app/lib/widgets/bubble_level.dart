import 'dart:math';
import 'package:flutter/material.dart';
import '../models/inclination_data.dart';

class BubbleLevelWidget extends StatelessWidget {
  final InclinationData data;
  final double size;

  const BubbleLevelWidget({
    super.key,
    required this.data,
    this.size = 260,
  });

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      size: Size(size, size),
      painter: _BubbleLevelPainter(data: data),
    );
  }
}

class _BubbleLevelPainter extends CustomPainter {
  final InclinationData data;

  _BubbleLevelPainter({required this.data});

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;
    final radius = size.width / 2 - 8;

    // ── Background circle
    final bgPaint = Paint()
      ..color = const Color(0xFF1A2A1A)
      ..style = PaintingStyle.fill;
    canvas.drawCircle(Offset(cx, cy), radius, bgPaint);

    // ── Outer ring
    final ringPaint = Paint()
      ..color = const Color(0xFF3A5A3A)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2;
    canvas.drawCircle(Offset(cx, cy), radius, ringPaint);

    // ── Level zone (target circle)
    final levelRadius = radius * 0.15;
    final levelZonePaint = Paint()
      ..color = const Color(0xFF2A5A2A)
      ..style = PaintingStyle.fill;
    canvas.drawCircle(Offset(cx, cy), levelRadius, levelZonePaint);
    final levelZoneBorder = Paint()
      ..color = const Color(0xFF4CAF50)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5;
    canvas.drawCircle(Offset(cx, cy), levelRadius, levelZoneBorder);

    // ── Crosshairs
    final crossPaint = Paint()
      ..color = const Color(0xFF3A5A3A)
      ..strokeWidth = 1;
    canvas.drawLine(Offset(cx, cy - radius + 8), Offset(cx, cy + radius - 8), crossPaint);
    canvas.drawLine(Offset(cx - radius + 8, cy), Offset(cx + radius - 8, cy), crossPaint);

    // ── Bubble position
    // Clamp bubble within circle
    final maxOffset = radius - 20;
    final rawX = (data.roll  / 10.0) * maxOffset;
    final rawY = (data.pitch / 10.0) * maxOffset;
    final dist = sqrt(rawX * rawX + rawY * rawY);
    double bx = rawX, by = rawY;
    if (dist > maxOffset) {
      bx = rawX / dist * maxOffset;
      by = rawY / dist * maxOffset;
    }

    final bubbleX = cx + bx;
    final bubbleY = cy + by;
    final bubbleRadius = radius * 0.12;

    final isLevel = data.isLevel;
    final bubbleColor = isLevel
        ? const Color(0xFF4CAF50)
        : (data.pitch.abs() < 3.0 && data.roll.abs() < 3.0)
            ? const Color(0xFFFF9800)
            : const Color(0xFFF44336);

    // Bubble shadow
    final shadowPaint = Paint()
      ..color = Colors.black.withOpacity(0.3)
      ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 4);
    canvas.drawCircle(Offset(bubbleX + 2, bubbleY + 2), bubbleRadius, shadowPaint);

    // Bubble fill
    final bubblePaint = Paint()
      ..shader = RadialGradient(
        colors: [bubbleColor.withOpacity(0.9), bubbleColor.withOpacity(0.6)],
      ).createShader(Rect.fromCircle(center: Offset(bubbleX, bubbleY), radius: bubbleRadius));
    canvas.drawCircle(Offset(bubbleX, bubbleY), bubbleRadius, bubblePaint);

    // Bubble border
    final bubbleBorder = Paint()
      ..color = bubbleColor
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5;
    canvas.drawCircle(Offset(bubbleX, bubbleY), bubbleRadius, bubbleBorder);

    // ── Degree markings
    final textPainter = TextPainter(textDirection: TextDirection.ltr);
    for (final angle in [0, 90, 180, 270]) {
      final rad = angle * pi / 180;
      final tx = cx + (radius - 16) * cos(rad);
      final ty = cy + (radius - 16) * sin(rad);
      final label = angle == 0 ? 'F' : angle == 90 ? 'R' : angle == 180 ? 'B' : 'L';
      textPainter.text = TextSpan(
        text: label,
        style: const TextStyle(color: Color(0xFF5A7A5A), fontSize: 11),
      );
      textPainter.layout();
      textPainter.paint(canvas, Offset(tx - textPainter.width / 2, ty - textPainter.height / 2));
    }
  }

  @override
  bool shouldRepaint(_BubbleLevelPainter old) =>
      old.data.pitch != data.pitch || old.data.roll != data.roll;
}
