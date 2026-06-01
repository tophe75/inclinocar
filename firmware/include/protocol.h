#pragma once

// ─── ESP-NOW channel ─────────────────────────────────────────
#define ESPNOW_CHANNEL  1

// ─── Message types ───────────────────────────────────────────
#define MSG_DATA        0x01   // Core → Satellite: pitch/roll data
#define MSG_PAIR_REQ    0x02   // Satellite → broadcast: looking for core
#define MSG_PAIR_ACK    0x03   // Core → Satellite: I am your core

// ─── Pairing timeout ─────────────────────────────────────────
#define PAIR_MODE_MS    30000  // Core stays in pair mode 30 seconds
#define PAIR_REQ_MS     500    // Satellite sends pair request every 500ms
#define SIGNAL_TIMEOUT  3000   // Show "No Signal" after 3s without data

// ─── Packet structures ───────────────────────────────────────
struct DataPacket {
  uint8_t  type;       // MSG_DATA
  float    pitch;
  float    roll;
};

struct PairPacket {
  uint8_t  type;       // MSG_PAIR_REQ or MSG_PAIR_ACK
  uint8_t  mac[6];     // sender MAC
};
