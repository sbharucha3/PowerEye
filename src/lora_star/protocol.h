#pragma once
#include <stdint.h>
#include "config.h"

// ---------- Packet types ----------
enum : uint8_t { PKT_POLL = 0xA1, PKT_REPLY = 0xD2 };

// ---------- Hub -> node commands (carried in that node's poll) ----------
enum : uint8_t { CMD_NONE = 0, CMD_IDENTIFY = 1, CMD_REBOOT = 2 };
#define CMD_ALL 0xFF

#define POLL_FLAG_FRESH 0x01   // hub has no history for this node: just send the newest sample

// Hub -> one node. "Send me your data. Here is what I already have, and here is the time."
struct __attribute__((packed)) Poll {
  uint8_t  type;          // PKT_POLL
  uint8_t  net;           // NET_ID
  uint8_t  id;            // node being polled
  uint8_t  flags;         // POLL_FLAG_*
  uint16_t cycle;
  int64_t  netMs;         // hub's network clock (ms) when this poll was sent
  uint16_t haveSeq;       // hub has every sample up to and including this seq
  uint64_t haveMask;      // bit i set = hub also has sample haveSeq + 1 + i
  uint8_t  cmd;           // CMD_*
  uint8_t  cmdSeq;        // changes for every new command (node ignores repeats)
  int16_t  cmdArg;
};

// ---------- Placeholder sensor payload — edit to match your real sensors ----------
// Stored as scaled integers to keep packets small.
struct __attribute__((packed)) SensorData {
  uint16_t voltage_cV;    // volts   x 100
  int16_t  current_cA;    // amps    x 100
  int32_t  power_dW;      // watts   x 10
  int16_t  temp_cC;       // deg C   x 100
  uint8_t  status;        // your status / fault bit flags
};

// One sample as sent over the air
struct __attribute__((packed)) WireSample {
  uint16_t   seq;         // node's sample counter
  uint32_t   ageMs;       // how old the sample is when sent; the hub turns this into a timestamp
  SensorData d;
};

// Node -> hub, only when polled
struct __attribute__((packed)) ReplyHeader {
  uint8_t  type;          // PKT_REPLY
  uint8_t  net;
  uint8_t  id;
  uint8_t  count;         // number of samples that follow
  uint16_t bootId;        // random per boot, so the hub notices node reboots
  uint16_t newestSeq;     // node's newest sample
  uint16_t buffered;      // samples stored on the node
  int8_t   pollRssi;      // how well the node heard the hub (dBm)
  int8_t   pollSnr4;      // SNR x 4
  int8_t   chipTempC;     // node's ESP32-S3 chip temperature
  uint8_t  radioResets;   // times the node has reset its radio since boot (health check)
};

struct __attribute__((packed)) Reply {
  ReplyHeader h;
  WireSample  smp[MAX_PER_REPLY];   // only `count` entries are sent
};

// Node-side history entry
struct Stored {
  uint16_t   seq;
  uint32_t   tLocalMs;    // node's own clock when sampled
  SensorData d;
};

// Hub-side history cell (one per node per time slot)
struct HistCell {
  SensorData d;
  uint8_t    valid;
};

// Hub-side table entry for each node
struct NodeInfo {
  bool       seen;
  bool       tracking;          // hub has a sample history for this node
  uint16_t   bootId;
  uint32_t   lastSeenMs;
  uint16_t   contig;            // every sample up to here received (or given up on)
  uint64_t   mask;              // received samples above contig
  uint16_t   newestSeq;
  uint16_t   buffered;
  uint32_t   polls, replies;    // link quality
  uint32_t   samplesRx, samplesLost, backfilled;
  uint8_t    failStreak;
  uint8_t    triesThisCycle;
  bool       answeredThisCycle;
  float      rssi, snr;         // hub's view of the node
  int8_t     nodeRssi;          // node's view of the hub
  float      nodeSnr;
  int8_t     chipTempC;         // node chip temperature
  uint8_t    radioResets;
  SensorData s;                 // latest sample
  int64_t    latestSampleNet;   // network time of that sample (ms)
};
