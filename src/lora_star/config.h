#pragma once
// ============================================================================
//  Shared configuration — must be IDENTICAL on the hub and every node
// ============================================================================

// ---------- Pins: Wio-SX1262 for XIAO ESP32S3 (B2B connector kit) ----------
#define LORA_SCK   7
#define LORA_MISO  8
#define LORA_MOSI  9
#define LORA_NSS   41
#define LORA_DIO1  39
#define LORA_RST   42
#define LORA_BUSY  40
#define LORA_RXEN  38
#define LED_PIN    LED_BUILTIN   // GPIO21, active LOW

// ---------- Radio ----------
#define FREQ_MHZ    915.0   // US915
#define BW_KHZ      500.0
#define SF          7
#define CR          5       // 4/5
#define SYNC_WORD   0x12
#define TX_POWER    22      // dBm, SX1262 maximum (~120 mA while transmitting)
#define PA_CURRENT_LIMIT 140.0   // mA, needed for 22 dBm
#define PREAMBLE    8
#define TCXO_V      1.8

// ---------- Polling ----------
#define NET_ID               0x5A   // rejects packets from other networks
#define MAX_NODES            50     // node IDs 1..MAX_NODES
#define CYCLE_MS             5000   // hub polls every known node once per cycle
#define MAX_TRIES            3      // polls per node per cycle (1 + 2 retries, if time allows)
#define OFFLINE_AFTER_FAILS  5      // missed cycles in a row before a node gets no retries
#define DISCOVERY_PER_CYCLE  4      // unknown IDs probed each cycle (finds new nodes)
#define NODE_REPLY_DELAY_MS  3      // node waits this long before replying (hub RX turnaround)
#define CYCLE_GUARD_MS       30     // keep this much free at the end of each cycle
#define NODE_TIMEOUT_MS      15000  // web page shows a node as offline after this

#define CYCLE_US ((int64_t)CYCLE_MS * 1000)

// ---------- Node sampling and logging ----------
// Every node samples on the SAME time grid (multiples of SAMPLE_MS on the
// hub's clock), so samples from different nodes share identical timestamps.
// Sampling is independent of polling: with a 5 s cycle and 3 s sampling,
// each poll brings back 1-2 new samples.
#define SAMPLE_MS        3000   // node sampling period
#define BUFFER_SAMPLES   1200   // samples kept on each node (1200 x 3 s = 60 min, ~24 KB RAM)
#define MAX_PER_REPLY    3      // samples per reply: newest + up to 2 older ones
#define BACKFILL_WINDOW  64     // hub can recover gaps up to 64 samples (~3 min at 3 s)

// ---------- Hub history ----------
// Stored in the XIAO's 8 MB PSRAM. Enable it: Tools > PSRAM > "OPI PSRAM".
#define HISTORY_SLOTS           9600   // 9600 x 3 s = 8 hours for all nodes (~5.8 MB)
#define HISTORY_SLOTS_NO_PSRAM  100    // fallback if PSRAM is off (5 minutes)

static_assert(MAX_NODES >= 1 && MAX_NODES <= 254, "MAX_NODES must be 1..254");
static_assert(MAX_PER_REPLY >= 1 && MAX_PER_REPLY <= 6, "MAX_PER_REPLY must be 1..6");
static_assert(BUFFER_SAMPLES >= BACKFILL_WINDOW && BUFFER_SAMPLES < 30000, "BUFFER_SAMPLES must be 64..29999");
static_assert((long)MAX_PER_REPLY * SAMPLE_MS > CYCLE_MS,
              "Replies cannot keep up with the sampling rate: raise MAX_PER_REPLY or SAMPLE_MS");

// ---------- Wi-Fi (hub only) ----------
// The hub always creates its own access point. Join it and open http://192.168.4.1
// Opening the page also sets the hub's clock from your phone/laptop.
#define AP_SSID   "LoRa-Hub"
#define AP_PASS   "lorahub123"     // at least 8 characters
// Optional: also join an existing WPA2-Personal network (leave "" to skip).
// If it has internet, the hub sets its clock from NTP. eduroam is not supported.
#define STA_SSID  ""
#define STA_PASS  ""
#define NTP_SERVER "pool.ntp.org"

// ---------- Power / heat ----------
// Moderate settings with headroom: still cooler than the original code
// (the CPU now idles instead of spinning), without cutting it fine.
// ESP32-S3 only runs at 240, 160 or 80 MHz (120 / 200 aren't available).
#define HUB_WIFI_TX_POWER      WIFI_POWER_11dBm   // fine for a phone/iPad in the same room
#define HUB_WIFI_TX_POWER_HOT  WIFI_POWER_8_5dBm  // used automatically if the hub chip gets hot
#define HUB_CPU_MHZ            160                // comfortable for Wi-Fi + web page + radio
#define NODE_CPU_MHZ           80                 // radio only, CPU mostly idle: plenty
#define CHIP_HOT_C             75                 // chip temperature that triggers the hot settings
#define CHIP_COOL_C            65                 // ...and goes back to normal below this

// ---------- Contingencies (self-recovery) ----------
#define WATCHDOG_S               10     // reboot automatically if the main loop hangs this long
#define RADIO_TX_FAILS_REINIT    3      // consecutive TX errors before resetting the radio
#define HUB_SILENT_CYCLES_REINIT 6      // cycles with zero replies (nodes known) before resetting the hub radio
#define NODE_SILENT_MS_REINIT    60000  // node resets its radio if not polled for this long
#define REPLY_TIMEOUT_MARGIN_MS  10    // extra wait for a reply beyond its airtime
#define WIFI_CHECK_MS            10000  // hub re-checks its Wi-Fi this often and restarts it if needed
