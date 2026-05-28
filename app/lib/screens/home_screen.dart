import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/ble_service.dart';
import '../widgets/bubble_level.dart';
import '../widgets/angle_readout.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleService>();

    return Scaffold(
      backgroundColor: const Color(0xFF0D1A0D),
      appBar: AppBar(
        backgroundColor: const Color(0xFF0D1A0D),
        title: Row(
          children: [
            const Text('InclinoCar',
              style: TextStyle(
                color: Color(0xFF4CAF50),
                fontWeight: FontWeight.w300,
                letterSpacing: 3,
              ),
            ),
            const Spacer(),
            _ConnectionBadge(status: ble.status),
          ],
        ),
      ),
      body: SafeArea(
        child: ble.isConnected
            ? _ConnectedView(ble: ble)
            : _DisconnectedView(ble: ble),
      ),
    );
  }
}

// ─── Connected View ────────────────────────────────────────────
class _ConnectedView extends StatelessWidget {
  final BleService ble;
  const _ConnectedView({required this.ble});

  @override
  Widget build(BuildContext context) {
    final data = ble.data;
    return Column(
      children: [
        const SizedBox(height: 16),

        // Level status banner
        AnimatedContainer(
          duration: const Duration(milliseconds: 300),
          margin: const EdgeInsets.symmetric(horizontal: 24),
          padding: const EdgeInsets.symmetric(vertical: 10),
          decoration: BoxDecoration(
            color: data.isLevel
                ? const Color(0xFF1A3A1A)
                : const Color(0xFF3A1A0A),
            borderRadius: BorderRadius.circular(8),
            border: Border.all(
              color: data.isLevel
                  ? const Color(0xFF4CAF50)
                  : const Color(0xFFFF9800),
            ),
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(
                data.isLevel ? Icons.check_circle : Icons.adjust,
                color: data.isLevel ? const Color(0xFF4CAF50) : const Color(0xFFFF9800),
                size: 18,
              ),
              const SizedBox(width: 8),
              Text(
                data.isLevel ? 'TENT IS LEVEL' : 'ADJUSTMENT NEEDED',
                style: TextStyle(
                  color: data.isLevel ? const Color(0xFF4CAF50) : const Color(0xFFFF9800),
                  fontWeight: FontWeight.w600,
                  letterSpacing: 2,
                  fontSize: 13,
                ),
              ),
            ],
          ),
        ),

        const SizedBox(height: 24),

        // Bubble level
        Center(child: BubbleLevelWidget(data: data, size: 240)),

        const SizedBox(height: 24),

        // Angle readout
        Container(
          margin: const EdgeInsets.symmetric(horizontal: 24),
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: const Color(0xFF111E11),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: const Color(0xFF2A3A2A)),
          ),
          child: AngleReadout(data: data),
        ),

        // Satellite data (if available)
        if (data.satPitch != null) ...[
          const SizedBox(height: 12),
          Container(
            margin: const EdgeInsets.symmetric(horizontal: 24),
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
            decoration: BoxDecoration(
              color: const Color(0xFF111E11),
              borderRadius: BorderRadius.circular(12),
              border: Border.all(color: const Color(0xFF2A3A2A)),
            ),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceAround,
              children: [
                const Text('CAR BODY',
                  style: TextStyle(color: Color(0xFF5A7A5A), fontSize: 11, letterSpacing: 2)),
                Text('P: ${data.satPitch!.toStringAsFixed(1)}°',
                  style: const TextStyle(color: Color(0xFF7AAA7A), fontSize: 14)),
                Text('R: ${data.satRoll!.toStringAsFixed(1)}°',
                  style: const TextStyle(color: Color(0xFF7AAA7A), fontSize: 14)),
              ],
            ),
          ),
        ],

        const Spacer(),

        // Disconnect button
        Padding(
          padding: const EdgeInsets.all(24),
          child: TextButton(
            onPressed: ble.disconnect,
            child: const Text('Disconnect',
              style: TextStyle(color: Color(0xFF5A7A5A))),
          ),
        ),
      ],
    );
  }
}

// ─── Disconnected View ─────────────────────────────────────────
class _DisconnectedView extends StatelessWidget {
  final BleService ble;
  const _DisconnectedView({required this.ble});

  @override
  Widget build(BuildContext context) {
    final isScanning = ble.status == BleStatus.scanning;
    final isConnecting = ble.status == BleStatus.connecting;

    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // Icon
            Container(
              padding: const EdgeInsets.all(28),
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                border: Border.all(color: const Color(0xFF2A4A2A), width: 2),
              ),
              child: const Icon(Icons.terrain,
                color: Color(0xFF4CAF50), size: 48),
            ),

            const SizedBox(height: 32),

            const Text('InclinoCar',
              style: TextStyle(
                color: Color(0xFF4CAF50),
                fontSize: 28,
                fontWeight: FontWeight.w200,
                letterSpacing: 6,
              ),
            ),

            const SizedBox(height: 8),

            const Text('Rooftop Tent Leveling',
              style: TextStyle(color: Color(0xFF5A7A5A), fontSize: 14, letterSpacing: 2),
            ),

            const SizedBox(height: 40),

            if (ble.errorMessage.isNotEmpty) ...[
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: const Color(0xFF2A1A1A),
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: const Color(0xFFF44336).withOpacity(0.5)),
                ),
                child: Text(ble.errorMessage,
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: Color(0xFFEF9A9A), fontSize: 13)),
              ),
              const SizedBox(height: 24),
            ],

            SizedBox(
              width: double.infinity,
              child: ElevatedButton(
                onPressed: (isScanning || isConnecting) ? null : ble.startScan,
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF1A4A1A),
                  foregroundColor: const Color(0xFF4CAF50),
                  padding: const EdgeInsets.symmetric(vertical: 16),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(8),
                    side: const BorderSide(color: Color(0xFF4CAF50)),
                  ),
                ),
                child: isScanning || isConnecting
                    ? Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          const SizedBox(
                            width: 16, height: 16,
                            child: CircularProgressIndicator(
                              strokeWidth: 2,
                              color: Color(0xFF4CAF50),
                            ),
                          ),
                          const SizedBox(width: 12),
                          Text(isScanning ? 'Scanning...' : 'Connecting...'),
                        ],
                      )
                    : const Text('Connect to InclinoCar',
                        style: TextStyle(fontSize: 15, letterSpacing: 1)),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ─── Connection Badge ──────────────────────────────────────────
class _ConnectionBadge extends StatelessWidget {
  final BleStatus status;
  const _ConnectionBadge({required this.status});

  @override
  Widget build(BuildContext context) {
    Color color;
    String label;
    switch (status) {
      case BleStatus.connected:    color = const Color(0xFF4CAF50); label = 'Connected'; break;
      case BleStatus.scanning:     color = const Color(0xFFFF9800); label = 'Scanning';  break;
      case BleStatus.connecting:   color = const Color(0xFFFF9800); label = 'Connecting'; break;
      default:                     color = const Color(0xFF5A5A5A); label = 'Offline';
    }
    return Row(
      children: [
        Container(width: 7, height: 7,
          decoration: BoxDecoration(color: color, shape: BoxShape.circle)),
        const SizedBox(width: 6),
        Text(label, style: TextStyle(color: color, fontSize: 12)),
      ],
    );
  }
}
