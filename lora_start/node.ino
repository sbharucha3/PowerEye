/*
  LoRa Star Network (hub polling): 1 hub + up to 50 nodes
  Hardware : Seeed XIAO ESP32-S3 + Wio-SX1262 (every device identical)
  Library  : RadioLib
  Tools    : PSRAM > "OPI PSRAM" (needed for 8 h of history on the hub)

  How it works:
    - Every 5 s the hub polls each known node in turn:
      POLL(node, network time, "here is what I already have") -> REPLY(samples).
      Only one radio talks at a time, so packets never collide.
    - Every poll carries the hub's clock, so all nodes share one network time
      (synced to within a few ms) and sample on the same 3 s grid. Samples
      from different nodes therefore have identical timestamps.
    - The hub turns network time into real date/time from NTP (if it joins a
      Wi-Fi network with internet) or from your phone/laptop when you open
      the web page.
    - Each node keeps 1 h of history. A reply carries the newest sample plus
      the oldest ones the hub is missing (backfill), so short outages lose nothing.
    - The hub stores 8 h of history for all nodes in PSRAM: chart + CSV download
      on the web page.

  Same firmware on every board. Configure each one over Serial (115200 baud).
  Settings are saved in flash:
      role hub | role node | id <1-50> | show | help | reboot
  Hub : table | csv | cmd <id|all> identify | cmd <id|all> reboot
  Node: stats | v (toggle verbose) | dump (print the stored history as CSV)
*/

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <sys/time.h>
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "config.h"
#include "protocol.h"
#include "web_page.h"

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, SPI);
Preferences prefs;
WebServer server(80);

volatile bool    irqFlag   = false;
volatile int64_t irqTimeUs = 0;     // when the last packet finished arriving
void IRAM_ATTR onDio1() {
  irqTimeUs = esp_timer_get_time();
  irqFlag = true;
}

bool    isHub  = false;
uint8_t nodeId = 0;
unsigned long ledOffAt = 0;

// Timing, computed from the real airtime at boot
unsigned long pollToaUs = 0, reply1ToaUs = 0, replyMaxToaUs = 0;
unsigned long replyTimeoutUs = 0, perNodeWorstUs = 0, perNodeTypicalUs = 0;

float   chipTempC = 0;           // this board's ESP32-S3 temperature, updated every 5 s
uint8_t radioResets = 0;

int64_t localMs() { return esp_timer_get_time() / 1000; }

// ---------------------------------------------------------------------------
void ledFlash(unsigned long ms) {
  digitalWrite(LED_PIN, LOW);
  ledOffAt = millis() + ms;
}

// Start (or restart) the radio with all settings applied
bool radioInit() {
  int st = radio.begin(FREQ_MHZ, BW_KHZ, SF, CR, SYNC_WORD, TX_POWER, PREAMBLE, TCXO_V, false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[RADIO] init failed, code %d\n", st);
    return false;
  }
  radio.setCurrentLimit(PA_CURRENT_LIMIT);
  radio.setOutputPower(TX_POWER);
  radio.setRfSwitchPins(LORA_RXEN, RADIOLIB_NC);
  radio.setDio2AsRfSwitch(true);
  radio.setDio1Action(onDio1);
  return true;
}

// Contingency: a stuck radio is reset in place instead of needing a power cycle
void radioReinit(const char* why) {
  Serial.printf("[RADIO] resetting radio (%s)\n", why);
  if (radioResets < 255) radioResets++;
  irqFlag = false;
  if (radioInit()) radio.startReceive();
}

bool radioTx(const void* buf, size_t len) {
  static uint8_t consecutiveFails = 0;
  int st = radio.transmit((uint8_t*)buf, len);
  irqFlag = false;                 // ignore the TX-done interrupt
  radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[TX] error %d\n", st);
    if (++consecutiveFails >= RADIO_TX_FAILS_REINIT) {
      consecutiveFails = 0;
      radioReinit("repeated TX errors");
    }
    return false;
  }
  consecutiveFails = 0;
  return true;
}

// Contingency: reboot automatically if the main loop ever hangs
void watchdogStart() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = { .timeout_ms = WATCHDOG_S * 1000, .idle_core_mask = 0, .trigger_panic = true };
  if (esp_task_wdt_init(&cfg) != ESP_OK) esp_task_wdt_reconfigure(&cfg);
#else
  esp_task_wdt_init(WATCHDOG_S, true);
#endif
  esp_task_wdt_add(NULL);
}

void printResetReason() {
  esp_reset_reason_t rr = esp_reset_reason();
  const char* why = nullptr;
  if (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) why = "watchdog (the code hung and recovered)";
  else if (rr == ESP_RST_BROWNOUT) why = "brownout (power supply dipped: check the cable/battery)";
  else if (rr == ESP_RST_PANIC) why = "crash";
  if (why) Serial.printf("NOTE: last reset was a %s\n", why);
}

// Returns packet length, or -1 on error / bad CRC.
int radioRead(uint8_t* buf, size_t maxLen, float& rssi, float& snr) {
  size_t len = radio.getPacketLength();
  if (len == 0 || len > maxLen) {
    radio.startReceive();
    return -1;
  }
  int st = radio.readData(buf, len);
  rssi = radio.getRSSI();
  snr  = radio.getSNR();
  radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) return -1;
  return (int)len;
}

// ---------------------------------------------------------------------------
void computeTiming() {
  pollToaUs     = (unsigned long)radio.getTimeOnAir(sizeof(Poll));
  reply1ToaUs   = (unsigned long)radio.getTimeOnAir(sizeof(ReplyHeader) + sizeof(WireSample));
  replyMaxToaUs = (unsigned long)radio.getTimeOnAir(sizeof(ReplyHeader) + MAX_PER_REPLY * sizeof(WireSample));
  replyTimeoutUs   = replyMaxToaUs + (NODE_REPLY_DELAY_MS + REPLY_TIMEOUT_MARGIN_MS) * 1000UL;
  perNodeWorstUs   = pollToaUs + replyTimeoutUs + 2000;
  perNodeTypicalUs = pollToaUs + reply1ToaUs + (NODE_REPLY_DELAY_MS + 3) * 1000UL;

  Serial.printf("Airtime: poll %.1f ms | reply %.1f ms (1 sample) to %.1f ms (%d samples)\n",
                pollToaUs / 1000.0, reply1ToaUs / 1000.0, replyMaxToaUs / 1000.0, MAX_PER_REPLY);
  float fullUs = (float)perNodeTypicalUs * MAX_NODES;
  Serial.printf("Per node ~%.1f ms. Polling %d nodes takes ~%.2f s of each %.1f s cycle; the rest is for retries.\n",
                perNodeTypicalUs / 1000.0, MAX_NODES, fullUs / 1e6, CYCLE_MS / 1000.0);
  if (fullUs > CYCLE_US - CYCLE_GUARD_MS * 1000LL) {
    Serial.println("WARNING: all nodes cannot be polled in one cycle. Raise CYCLE_MS, lower MAX_NODES, or use a faster SF/BW.");
  }
}

void printConfig() {
  Serial.printf("Role : %s", isHub ? "HUB" : "NODE");
  if (!isHub) Serial.printf("   ID: %u%s", nodeId, nodeId == 0 ? " (NOT SET, type: id <1-50>)" : "");
  Serial.printf("\nRadio: %.1f MHz, BW %.0f kHz, SF%d, CR 4/%d, %d dBm\n",
                FREQ_MHZ, BW_KHZ, SF, CR, TX_POWER);
  Serial.printf("Poll : every %d ms, up to %d tries per node per cycle\n", CYCLE_MS, MAX_TRIES);
  Serial.printf("Data : sampled every %d ms on a shared time grid\n", SAMPLE_MS);
  if (!isHub) {
    Serial.printf("Log  : %d samples kept on this node (%.1f min, %.1f KB)\n",
                  BUFFER_SAMPLES, BUFFER_SAMPLES * (SAMPLE_MS / 60000.0),
                  BUFFER_SAMPLES * sizeof(Stored) / 1024.0);
  }
}

void printHelp() {
  Serial.println("\nSetup : role hub | role node | id <1-50> | show | reboot | help");
  if (isHub) Serial.println("Hub   : table | csv | cmd <id|all> identify | cmd <id|all> reboot");
  else       Serial.println("Node  : stats | v (toggle verbose) | dump (print stored history)");
}

void processLine(String& line) {
  if (line == "role hub" || line == "role node") {
    prefs.putBool("hub", line == "role hub");
    Serial.println("Saved. Rebooting...");
    delay(200);
    ESP.restart();
  } else if (line.startsWith("id ")) {
    int v = line.substring(3).toInt();
    if (v < 1 || v > MAX_NODES) {
      Serial.printf("ID must be 1-%d\n", MAX_NODES);
    } else {
      nodeId = (uint8_t)v;
      prefs.putUChar("id", nodeId);
      Serial.printf("Saved node ID %u\n", nodeId);
    }
  } else if (line == "show") {
    printConfig();
  } else if (line == "reboot") {
    ESP.restart();
  } else if (line == "help" || line == "?") {
    printHelp();
  } else if (isHub ? hubCommand(line) : nodeCommand(line)) {
    // handled
  } else {
    Serial.println("Unknown command, type help");
  }
}

// Non-blocking line reader so Serial input never stalls the radio
void handleSerial() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      buf.trim();
      if (buf.length()) processLine(buf);
      buf = "";
    } else if (buf.length() < 80) {
      buf += c;
    }
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 2000) {}

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  prefs.begin("lorastar", false);
  isHub  = prefs.getBool("hub", false);
  nodeId = prefs.getUChar("id", 0);

  Serial.println("\n=== XIAO ESP32-S3 LoRa polling network ===");
  printResetReason();

  // Contingency: retry the radio a few times, then reboot, instead of stopping forever
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  for (int tries = 1; !radioInit(); tries++) {
    Serial.println("Check the Wio-SX1262 is seated and the pin defines are right.");
    if (tries >= 5) { Serial.println("Rebooting..."); delay(500); ESP.restart(); }
    for (int i = 0; i < 20; i++) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(100); }
  }

  printConfig();
  computeTiming();

  if (isHub) {
    hubSetup();
  } else {
    WiFi.mode(WIFI_OFF);              // nodes never use Wi-Fi or Bluetooth
    btStop();
    setCpuFrequencyMhz(NODE_CPU_MHZ);
    nodeSetup();
  }

  radio.startReceive();
  chipTempC = temperatureRead();
  printHelp();
  watchdogStart();
}

void loop() {
  esp_task_wdt_reset();

  static unsigned long lastTempMs = 0;
  if (millis() - lastTempMs > 5000) {
    lastTempMs = millis();
    chipTempC = temperatureRead();
  }

  if (irqFlag) {
    int64_t rxUs = irqTimeUs;
    irqFlag = false;
    uint8_t buf[sizeof(Reply) + 8];
    float rssi = 0, snr = 0;
    int len = radioRead(buf, sizeof(buf), rssi, snr);
    if (len > 0) {
      if (isHub) hubOnPacket(buf, len, rssi, snr, rxUs);
      else       nodeOnPacket(buf, len, rssi, snr, rxUs);
    }
  }

  if (isHub) hubLoop();
  else       nodeLoop();

  handleSerial();

  if (ledOffAt && millis() > ledOffAt) {
    digitalWrite(LED_PIN, HIGH);
    ledOffAt = 0;
  }

  // Let the CPU idle between checks instead of spinning flat out (big heat
  // saver). Radio packets wait in the SX1262's buffer, so 1 ms costs nothing.
  if (!irqFlag) delay(1);
}
