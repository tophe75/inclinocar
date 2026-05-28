# InclinoCar BLE Service Specification

## Service

| Field | Value |
|-------|-------|
| UUID | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` |
| Name | InclinoCar |

---

## Characteristics

### Tent Pitch
| Field | Value |
|-------|-------|
| UUID | `beb5483e-36e1-4688-b7f5-ea07361b26a8` |
| Properties | READ, NOTIFY |
| Format | IEEE 754 float32, little-endian, 4 bytes |
| Unit | Degrees |
| Range | -90.0 to +90.0 |
| Update rate | 20 Hz (every 50 ms) |

Positive = front of tent is higher than rear.

---

### Tent Roll
| Field | Value |
|-------|-------|
| UUID | `beb5483f-36e1-4688-b7f5-ea07361b26a8` |
| Properties | READ, NOTIFY |
| Format | IEEE 754 float32, little-endian, 4 bytes |
| Unit | Degrees |
| Range | -90.0 to +90.0 |

Positive = right side is higher than left side.

---

### Satellite Pitch (optional)
| Field | Value |
|-------|-------|
| UUID | `beb54840-36e1-4688-b7f5-ea07361b26a8` |
| Properties | READ, NOTIFY |
| Format | IEEE 754 float32, little-endian, 4 bytes |
| Notes | Only notified when satellite unit is present and connected via ESP-NOW |

---

### Satellite Roll (optional)
| Field | Value |
|-------|-------|
| UUID | `beb54841-36e1-4688-b7f5-ea07361b26a8` |
| Properties | READ, NOTIFY |
| Format | IEEE 754 float32, little-endian, 4 bytes |
| Notes | Only notified when satellite unit is present and connected via ESP-NOW |

---

## Data Format

All characteristics send a raw 4-byte IEEE 754 float (little-endian).

**Example — Flutter parsing:**
```dart
import 'dart:typed_data';

double parseFloat(List<int> bytes) {
  final data = Uint8List.fromList(bytes);
  return ByteData.sublistView(data).getFloat32(0, Endian.little);
}
```

**Example — Python parsing (for testing):**
```python
import struct
value = struct.unpack('<f', bytes([0x00, 0x00, 0x48, 0x41]))[0]
# → 12.5 degrees
```

---

## Leveling Logic

| Condition | State |
|-----------|-------|
| abs(pitch) ≤ 0.5° AND abs(roll) ≤ 0.5° | ✅ Level |
| abs(pitch) ≤ 2.0° AND abs(roll) ≤ 2.0° | ⚠️ Close |
| Either axis > 2.0° | ❌ Adjustment needed |

---

## ESP-NOW Packet (Satellite → Core)

```c
struct SatelliteData {
  float pitch;  // 4 bytes, little-endian
  float roll;   // 4 bytes, little-endian
};
// Total: 8 bytes
```
