// ============================================================================
//  HUB: owns the network clock, polls every node each cycle, retries misses,
//       requests backfill, stores time-aligned history, serves the web page
// ============================================================================

NodeInfo nodes[MAX_NODES + 1];        // index = node id (0 unused)
SemaphoreHandle_t nodesMutex;         // shared with the web task

enum : uint8_t { H_IDLE, H_POLLING, H_WAIT };
uint8_t  hState = H_IDLE;
int64_t  cycleStartUs = 0, nextCycleUs = 0, waitUntilUs = 0;
uint16_t hubCycle = 0;
uint8_t  curTarget = 0;

// Poll queue for the current cycle: first pass, then retries appended
uint8_t  pollQueue[MAX_NODES * MAX_TRIES + DISCOVERY_PER_CYCLE + 1];
uint16_t qHead = 0, qTail = 0;
uint8_t  discoverNext = 1;

// Stats for the current / last cycle
uint16_t cPolled = 0, cAnswered = 0, cRetries = 0, cBackfilled = 0;
uint16_t lastPolled = 0, lastAnswered = 0, lastRetries = 0, lastCycleMs = 0;
bool     csvOut = false;

// Pending command: added to that node's polls for 3 cycles
uint8_t pendTarget = 0, pendCmd = CMD_NONE, pendRepeats = 0, cmdSeqCounter = 0;
int16_t pendArg = 0;

// ---------- Clock ----------
// Network time = hub uptime in ms + 1 h (never jumps; the extra hour leaves room
// for samples a node took before the hub booted). Real time = network time + offset.
#define NET_BASE_MS 3600000LL
enum : uint8_t { CLK_NONE = 0, CLK_BROWSER = 1, CLK_NTP = 2 };
int64_t  epochOffsetMs = 0;
uint8_t  clockSource = CLK_NONE;
bool     useSta = false;
int64_t  lastNtpCheckMs = 0;
uint32_t hubBootId = 0;           // lets the phone notice a hub reboot

int64_t netFromUs(int64_t us) { return us / 1000 + NET_BASE_MS; }
int64_t netNow() { return netFromUs(esp_timer_get_time()); }

// ---------- Contingencies ----------
uint8_t  silentCycles = 0;        // cycles in a row where no known node answered
bool     wifiHot = false;         // Wi-Fi power stepped down because the chip is hot
unsigned long lastWifiCheckMs = 0, lastStaRetryMs = 0;

// Keep the hub's Wi-Fi up and its temperature in check
void hubHealthCheck() {
  if (millis() - lastWifiCheckMs < WIFI_CHECK_MS) return;
  lastWifiCheckMs = millis();

  // Thermal step-down with hysteresis
  if (!wifiHot && chipTempC >= CHIP_HOT_C) {
    wifiHot = true;
    WiFi.setTxPower(HUB_WIFI_TX_POWER_HOT);
    Serial.printf("[HUB] chip at %.0f C: lowering Wi-Fi power\n", chipTempC);
  } else if (wifiHot && chipTempC <= CHIP_COOL_C) {
    wifiHot = false;
    WiFi.setTxPower(HUB_WIFI_TX_POWER);
    Serial.printf("[HUB] chip back to %.0f C: normal Wi-Fi power\n", chipTempC);
  }

  // Access point still running? Restart it if not.
  if (!(WiFi.getMode() & WIFI_MODE_AP) || WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println("[HUB] Wi-Fi access point was down: restarting it");
    WiFi.mode(useSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    WiFi.setTxPower(wifiHot ? HUB_WIFI_TX_POWER_HOT : HUB_WIFI_TX_POWER);
  }

  // Optional home/lab network: reconnect if it dropped
  if (useSta && WiFi.status() != WL_CONNECTED && millis() - lastStaRetryMs > 30000) {
    lastStaRetryMs = millis();
    WiFi.reconnect();
  }

  if (ESP.getFreeHeap() < 30000) {
    Serial.printf("[HUB] warning: low memory (%lu bytes free)\n", (unsigned long)ESP.getFreeHeap());
  }
}

const char* clockName() {
  return clockSource == CLK_NTP ? "NTP" : clockSource == CLK_BROWSER ? "browser" : "";
}

// "2026-11-03T14:05:03.000Z" when the clock is set, otherwise "T+123.456" (seconds since hub boot)
void fmtTime(int64_t netMs, char* out, size_t n) {
  if (clockSource != CLK_NONE) {
    int64_t e = netMs + epochOffsetMs;
    time_t secs = (time_t)(e / 1000);
    struct tm tmv;
    gmtime_r(&secs, &tmv);
    size_t k = strftime(out, n, "%Y-%m-%dT%H:%M:%S", &tmv);
    snprintf(out + k, n - k, ".%03dZ", (int)(e % 1000));
  } else {
    snprintf(out, n, "T+%.3f", (netMs - NET_BASE_MS) / 1000.0);
  }
}

void checkNtp() {
  if (!useSta || clockSource == CLK_NTP || WiFi.status() != WL_CONNECTED) return;
  if (localMs() - lastNtpCheckMs < 5000) return;
  lastNtpCheckMs = localMs();
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec > 1700000000) {                  // SNTP has set the system clock
    epochOffsetMs = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 - netNow();
    clockSource = CLK_NTP;
    Serial.println("[HUB] clock set from NTP");
  }
}

// ---------- History (time-aligned grid, one row per SAMPLE_MS slot) ----------
// Row r holds slot histRowSlot[r]; each row has one cell per node. Written only
// by the radio loop; the web task reads it without locking (a rare torn read
// only affects one point on the chart).
HistCell* histCells   = nullptr;
uint32_t* histRowSlot = nullptr;
uint32_t  histSlots   = 0;
uint32_t  histNewest  = 0;
bool      histAny     = false;
#define   SLOT_EMPTY  0xFFFFFFFFUL
#define   MAX_POINTS  600        // max points per series sent to the chart

void histInit() {
  bool ps = psramFound();
  uint32_t rows = ps ? HISTORY_SLOTS : HISTORY_SLOTS_NO_PSRAM;
  if (ps) {
    histCells   = (HistCell*)ps_calloc((size_t)rows * MAX_NODES, sizeof(HistCell));
    histRowSlot = (uint32_t*)ps_malloc((size_t)rows * sizeof(uint32_t));
  } else {
    histCells   = (HistCell*)calloc((size_t)rows * MAX_NODES, sizeof(HistCell));
    histRowSlot = (uint32_t*)malloc((size_t)rows * sizeof(uint32_t));
  }
  if (!histCells || !histRowSlot) {
    Serial.println("History: allocation FAILED, history disabled");
    histSlots = 0;
    return;
  }
  histSlots = rows;
  for (uint32_t i = 0; i < rows; i++) histRowSlot[i] = SLOT_EMPTY;
  Serial.printf("History: %lu slots x %d nodes = %.1f h at %d s (%s, %.1f MB)\n",
                (unsigned long)rows, MAX_NODES, rows * (SAMPLE_MS / 3600000.0), SAMPLE_MS / 1000,
                ps ? "PSRAM" : "no PSRAM: enable Tools > PSRAM > OPI PSRAM for 8 h",
                (double)rows * MAX_NODES * sizeof(HistCell) / 1048576.0);
}

uint32_t slotOf(int64_t netMs) { return (uint32_t)((netMs + SAMPLE_MS / 2) / SAMPLE_MS); }

void histPut(uint8_t id, int64_t tNet, const SensorData& d) {
  if (!histSlots || tNet < 0) return;
  uint32_t s = slotOf(tNet);
  if (histAny && s + histSlots <= histNewest) return;          // older than the history kept
  uint32_t row = s % histSlots;
  if (histRowSlot[row] != s) {
    if (histRowSlot[row] != SLOT_EMPTY && histRowSlot[row] > s) return;
    memset(&histCells[(size_t)row * MAX_NODES], 0, MAX_NODES * sizeof(HistCell));
    histRowSlot[row] = s;
  }
  HistCell& c = histCells[(size_t)row * MAX_NODES + (id - 1)];
  c.d = d;
  c.valid = 1;
  if (!histAny || s > histNewest) histNewest = s;
  histAny = true;
}

const HistCell* histGet(uint8_t id, uint32_t s) {
  if (!histSlots) return nullptr;
  uint32_t row = s % histSlots;
  if (histRowSlot[row] != s) return nullptr;
  const HistCell* c = &histCells[(size_t)row * MAX_NODES + (id - 1)];
  return c->valid ? c : nullptr;
}

float metricOf(const SensorData& d, char m) {
  switch (m) {
    case 'v': return d.voltage_cV / 100.0f;
    case 'a': return d.current_cA / 100.0f;
    case 't': return d.temp_cC / 100.0f;
    default:  return d.power_dW / 10.0f;
  }
}

// ---------------------------------------------------------------------------
void hubQueueCmd(uint8_t target, uint8_t cmd, int16_t arg) {
  xSemaphoreTake(nodesMutex, portMAX_DELAY);
  pendTarget = target;
  pendCmd = cmd;
  pendArg = arg;
  pendRepeats = 3;
  if (++cmdSeqCounter == 0) cmdSeqCounter = 1;
  xSemaphoreGive(nodesMutex);
  Serial.printf("[HUB] queued %s for %s\n", cmd == CMD_IDENTIFY ? "identify" : "reboot",
                target == CMD_ALL ? "all nodes" : String(target).c_str());
}

uint16_t nodeBacklog(const NodeInfo& n) {
  if (!n.tracking) return 0;
  uint16_t span = n.newestSeq - n.contig;
  if (span >= 32768) return 0;
  int have = __builtin_popcountll(n.mask);
  return span > have ? span - have : 0;
}

// Record that a sample arrived. Returns false if the hub already had it.
bool hubAccept(NodeInfo& n, uint16_t seq) {
  uint16_t d = seq - n.contig;                 // 1 = the next sample expected
  if (d == 0 || d >= 32768) return false;      // already have it (or older)
  d -= 1;
  if (d >= BACKFILL_WINDOW) {                  // too far ahead: give up on the oldest gaps
    uint16_t shift = d - (BACKFILL_WINDOW - 1);
    for (uint16_t i = 0; i < shift; i++) {
      if (!(n.mask & 1ULL)) n.samplesLost++;
      n.mask >>= 1;
      n.contig++;
    }
    d = BACKFILL_WINDOW - 1;
  }
  if ((n.mask >> d) & 1ULL) return false;      // duplicate
  n.mask |= (1ULL << d);
  while (n.mask & 1ULL) { n.mask >>= 1; n.contig++; }
  return true;
}

// ---------------------------------------------------------------------------
void hubStartCycle(int64_t now) {
  hubCycle++;
  cycleStartUs = now;
  nextCycleUs += CYCLE_US;
  if (nextCycleUs <= now) nextCycleUs = now + CYCLE_US;

  qHead = qTail = 0;
  cPolled = cAnswered = cRetries = cBackfilled = 0;

  for (int id = 1; id <= MAX_NODES; id++) {
    nodes[id].triesThisCycle = 0;
    nodes[id].answeredThisCycle = false;
    if (nodes[id].seen) pollQueue[qTail++] = id;      // known nodes first
  }
  // Probe a few unknown IDs so new nodes are found
  int added = 0;
  for (int k = 0; k < MAX_NODES && added < DISCOVERY_PER_CYCLE; k++) {
    uint8_t id = discoverNext;
    discoverNext = discoverNext % MAX_NODES + 1;
    if (!nodes[id].seen) { pollQueue[qTail++] = id; added++; }
  }
  hState = H_POLLING;
}

void hubEndCycle(bool outOfTime) {
  hState = H_IDLE;
  lastPolled = cPolled;
  lastAnswered = cAnswered;
  lastRetries = cRetries;
  lastCycleMs = (uint16_t)((esp_timer_get_time() - cycleStartUs) / 1000);

  xSemaphoreTake(nodesMutex, portMAX_DELAY);
  if (pendRepeats > 0) pendRepeats--;
  String missed;
  int known = 0;
  for (int id = 1; id <= MAX_NODES; id++) {
    NodeInfo& n = nodes[id];
    if (!n.seen) continue;
    known++;
    if (!n.answeredThisCycle) {
      if (n.failStreak < 255) n.failStreak++;
      missed += ' ';
      missed += id;
    }
  }
  xSemaphoreGive(nodesMutex);

  // Contingency: if every known node goes quiet at once, the hub radio is the
  // likely culprit. Reset it.
  if (known > 0 && cAnswered == 0) {
    if (++silentCycles >= HUB_SILENT_CYCLES_REINIT) {
      silentCycles = 0;
      radioReinit("no node has answered for several cycles");
    }
  } else {
    silentCycles = 0;
  }

  Serial.printf("[HUB] cycle %u: %u/%d answered, %u retries, %u backfilled, %u ms%s%s%s\n",
                hubCycle, cAnswered, known, cRetries, cBackfilled, lastCycleMs,
                missed.length() ? " | missed:" : "", missed.c_str(),
                outOfTime ? " | RAN OUT OF TIME" : "");
}

void hubSendPoll(uint8_t id) {
  NodeInfo& n = nodes[id];
  Poll p = {};
  p.type  = PKT_POLL;
  p.net   = NET_ID;
  p.id    = id;
  p.cycle = hubCycle;

  xSemaphoreTake(nodesMutex, portMAX_DELAY);
  if (!n.tracking) {
    p.flags |= POLL_FLAG_FRESH;
  } else {
    p.haveSeq  = n.contig;
    p.haveMask = n.mask;
  }
  if (pendRepeats > 0 && (pendTarget == id || pendTarget == CMD_ALL)) {
    p.cmd    = pendCmd;
    p.cmdSeq = cmdSeqCounter;
    p.cmdArg = pendArg;
  }
  if (n.triesThisCycle == 0) cPolled++;
  else                       cRetries++;
  n.triesThisCycle++;
  n.polls++;
  xSemaphoreGive(nodesMutex);

  curTarget = id;
  p.netMs = netNow();                  // stamp the clock as late as possible
  radioTx(&p, sizeof(p));
  waitUntilUs = esp_timer_get_time() + replyTimeoutUs;
  hState = H_WAIT;
}

void hubPollFailed(uint8_t id) {
  NodeInfo& n = nodes[id];
  if (!n.seen || n.answeredThisCycle) return;            // discovery probe: no retry
  bool offline = n.failStreak >= OFFLINE_AFTER_FAILS;    // don't burn retries on dead nodes
  if (!offline && n.triesThisCycle < MAX_TRIES && qTail < sizeof(pollQueue)) {
    pollQueue[qTail++] = id;                             // retry later this cycle
  }
}

void hubLoop() {
  checkNtp();
  hubHealthCheck();
  int64_t now = esp_timer_get_time();

  if (hState == H_IDLE) {
    if (now < nextCycleUs) return;
    hubStartCycle(now);
  }

  if (hState == H_WAIT) {
    if (now < waitUntilUs) return;
    hubPollFailed(curTarget);                            // timed out
    hState = H_POLLING;
  }

  if (hState == H_POLLING) {
    if (qHead >= qTail) { hubEndCycle(false); return; }
    if (now + (int64_t)perNodeWorstUs > cycleStartUs + CYCLE_US - CYCLE_GUARD_MS * 1000LL) {
      bool skippedFirstPass = false;                     // only warn if a node got no poll at all
      for (uint16_t i = qHead; i < qTail; i++)
        if (nodes[pollQueue[i]].triesThisCycle == 0 && nodes[pollQueue[i]].seen) skippedFirstPass = true;
      hubEndCycle(skippedFirstPass);
      return;
    }
    hubSendPoll(pollQueue[qHead++]);
  }
}

void hubOnPacket(const uint8_t* buf, int len, float rssi, float snr, int64_t rxUs) {
  if (len < (int)sizeof(ReplyHeader) || len > (int)sizeof(Reply)) return;
  Reply r = {};
  memcpy(&r, buf, len);
  ReplyHeader h = r.h;
  if (h.type != PKT_REPLY || h.net != NET_ID) return;
  if (h.id < 1 || h.id > MAX_NODES || h.count > MAX_PER_REPLY) return;
  if (len != (int)(sizeof(ReplyHeader) + h.count * sizeof(WireSample))) return;

  // The node measured each sample's age just before replying. Work back to
  // the moment it was taken, then snap it to the shared sampling grid.
  int64_t replyStartNet = netFromUs(rxUs) - NODE_REPLY_DELAY_MS
                          - (int64_t)(radio.getTimeOnAir(len) / 1000);

  uint32_t   nowMs = millis();
  WireSample outS[MAX_PER_REPLY];
  int64_t    outT[MAX_PER_REPLY];
  bool       outBf[MAX_PER_REPLY];
  int        nOut = 0;

  xSemaphoreTake(nodesMutex, portMAX_DELAY);
  NodeInfo& n = nodes[h.id];
  if (!n.seen) {                       // first contact: start link stats from here
    n.polls = 1; n.replies = 0;
    n.samplesRx = n.samplesLost = n.backfilled = 0;
  }
  n.seen = true;
  n.lastSeenMs = nowMs;
  n.replies++;
  n.answeredThisCycle = true;
  n.failStreak = 0;
  n.rssi = rssi;
  n.snr  = snr;
  n.nodeRssi = h.pollRssi;
  n.nodeSnr  = h.pollSnr4 / 4.0f;
  n.buffered = h.buffered;
  n.chipTempC   = h.chipTempC;
  n.radioResets = h.radioResets;

  if (!n.tracking || n.bootId != h.bootId) {   // new node, node rebooted, or hub rebooted:
    uint16_t back = h.buffered < BACKFILL_WINDOW ? h.buffered : BACKFILL_WINDOW;
    if (back == 0) back = 1;
    n.tracking = true;                         // ...also pull the last ~3 min the node has
    n.bootId   = h.bootId;
    n.contig   = h.newestSeq - back;
    n.mask     = 0;
  }
  n.newestSeq = h.newestSeq;

  for (int i = 0; i < h.count; i++) {
    WireSample w = r.smp[i];
    if (!hubAccept(n, w.seq)) continue;
    int64_t tNet = replyStartNet - (int64_t)w.ageMs;
    int64_t grid = (int64_t)slotOf(tNet) * SAMPLE_MS;
    if (llabs(tNet - grid) < 250) tNet = grid;   // synced nodes land exactly on the grid
    n.samplesRx++;
    bool bf = (w.seq != h.newestSeq);
    if (bf) { n.backfilled++; cBackfilled++; }
    else    { n.s = w.d; n.latestSampleNet = tNet; }
    outS[nOut] = w;
    outT[nOut] = tNet;
    outBf[nOut] = bf;
    nOut++;
  }
  xSemaphoreGive(nodesMutex);

  for (int i = 0; i < nOut; i++) histPut(h.id, outT[i], outS[i].d);

  if (hState == H_WAIT && h.id == curTarget) {
    cAnswered++;
    hState = H_POLLING;                        // poll the next node right away
  }
  ledFlash(10);

  if (csvOut) {
    char ts[32];
    for (int i = 0; i < nOut; i++) {
      const WireSample& w = outS[i];
      fmtTime(outT[i], ts, sizeof(ts));
      Serial.printf("CSV,%s,%u,%u,%.2f,%.2f,%.1f,%.2f,%u,%d,%.1f,%.1f\n",
                    ts, h.id, w.seq,
                    w.d.voltage_cV / 100.0, w.d.current_cA / 100.0, w.d.power_dW / 10.0,
                    w.d.temp_cC / 100.0, w.d.status, outBf[i] ? 1 : 0, rssi, snr);
    }
  }
}

void hubPrintTable() {
  Serial.println("\n ID  seen(s)      V       A         W       C    RSSI   SNR  nodeRSSI   link    data  backlog");
  int count = 0;
  for (int id = 1; id <= MAX_NODES; id++) {
    NodeInfo& n = nodes[id];
    if (!n.seen) continue;
    count++;
    unsigned long tot = n.samplesRx + n.samplesLost;
    Serial.printf("%3d %8.1f %7.2f %7.2f %9.1f %7.2f %7.1f %5.1f %9d %6.1f%% %6.1f%% %7u\n",
                  id, (millis() - n.lastSeenMs) / 1000.0,
                  n.s.voltage_cV / 100.0, n.s.current_cA / 100.0, n.s.power_dW / 10.0, n.s.temp_cC / 100.0,
                  n.rssi, n.snr, n.nodeRssi,
                  n.polls ? 100.0 * n.replies / n.polls : 0.0,
                  tot ? 100.0 * n.samplesRx / tot : 0.0,
                  nodeBacklog(n));
  }
  if (!count) Serial.println("  (no nodes heard yet)");
}

bool hubCommand(String& line) {
  if (line == "table") { hubPrintTable(); return true; }
  if (line == "csv") {
    csvOut = !csvOut;
    Serial.printf("CSV output %s\n", csvOut ? "ON" : "OFF");
    if (csvOut) Serial.println("CSV,time,id,seq,V,A,W,C,status,backfill,rssi,snr");
    return true;
  }
  if (line.startsWith("cmd ")) {
    int sp = line.indexOf(' ', 4);
    if (sp < 0) { Serial.println("Usage: cmd <id|all> identify|reboot"); return true; }
    String who  = line.substring(4, sp);
    String what = line.substring(sp + 1);
    what.trim();
    uint8_t cmd = (what == "identify") ? CMD_IDENTIFY : (what == "reboot") ? CMD_REBOOT : CMD_NONE;
    int id = (who == "all") ? CMD_ALL : who.toInt();
    if (cmd == CMD_NONE || (id != CMD_ALL && (id < 1 || id > MAX_NODES))) {
      Serial.println("Usage: cmd <id|all> identify|reboot");
    } else {
      hubQueueCmd((uint8_t)id, cmd, 0);
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
//  Web server (its own task on core 0, so radio timing on core 1 is unaffected)
// ---------------------------------------------------------------------------
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleApiTime() {
  if (!server.hasArg("ms")) { server.send(400, "text/plain", "missing ms"); return; }
  long long ms = strtoll(server.arg("ms").c_str(), nullptr, 10);
  // Set once per hub boot, so every timestamp the phone saves stays on the same
  // grid. (NTP, if available, takes over.)
  if (ms > 1700000000000LL && clockSource == CLK_NONE) {
    epochOffsetMs = (int64_t)ms - netNow();
    clockSource = CLK_BROWSER;
    Serial.println("[HUB] clock set from browser");
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

void handleApiNodes() {
  static NodeInfo snap[MAX_NODES + 1];
  xSemaphoreTake(nodesMutex, portMAX_DELAY);
  memcpy(snap, nodes, sizeof(nodes));
  xSemaphoreGive(nodesMutex);

  unsigned long now = millis();
  String j;
  j.reserve(400 + MAX_NODES * 220);
  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"cycle\":%u,\"polled\":%u,\"answered\":%u,\"retries\":%u,\"ms\":%u,\"timeout\":%d,"
           "\"sample\":%d,\"clock\":\"%s\",\"hubChip\":%.0f,\"hubHot\":%s,\"hubRadioResets\":%u,\"nodes\":[",
           hubCycle, lastPolled, lastAnswered, lastRetries, lastCycleMs, NODE_TIMEOUT_MS / 1000,
           SAMPLE_MS, clockName(), chipTempC, wifiHot ? "true" : "false", radioResets);
  j += buf;
  bool first = true;
  for (int id = 1; id <= MAX_NODES; id++) {
    NodeInfo& n = snap[id];
    if (!n.seen) continue;
    snprintf(buf, sizeof(buf),
             "%s{\"id\":%d,\"age\":%.1f,\"v\":%.2f,\"a\":%.2f,\"w\":%.1f,\"t\":%.2f,\"st\":%u,"
             "\"rssi\":%.1f,\"snr\":%.1f,\"nrssi\":%d,\"nsnr\":%.1f,"
             "\"polls\":%lu,\"replies\":%lu,\"rx\":%lu,\"lost\":%lu,\"backfilled\":%lu,"
             "\"backlog\":%u,\"buffered\":%u,\"chip\":%d,\"resets\":%u}",
             first ? "" : ",", id, (now - n.lastSeenMs) / 1000.0,
             n.s.voltage_cV / 100.0, n.s.current_cA / 100.0, n.s.power_dW / 10.0, n.s.temp_cC / 100.0,
             n.s.status, n.rssi, n.snr, n.nodeRssi, n.nodeSnr,
             (unsigned long)n.polls, (unsigned long)n.replies,
             (unsigned long)n.samplesRx, (unsigned long)n.samplesLost, (unsigned long)n.backfilled,
             nodeBacklog(n), n.buffered, n.chipTempC, n.radioResets);
    j += buf;
    first = false;
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// Work out which slots a "last N minutes" request covers
void histWindow(long mins, uint32_t& start, uint32_t& count) {
  start = 0; count = 0;
  if (!histAny || !histSlots) return;
  if (mins < 1) mins = 1;
  uint64_t want = (uint64_t)mins * 60000ULL / SAMPLE_MS;
  if (want > histSlots) want = histSlots;
  if (want < 2) want = 2;
  uint32_t newest = histNewest;
  start = (newest + 1 >= want) ? (uint32_t)(newest + 1 - want) : 0;
  count = newest + 1 - start;
}

void handleApiHistory() {
  char m = server.hasArg("m") && server.arg("m").length() ? server.arg("m")[0] : 'w';
  long mins = server.hasArg("min") ? server.arg("min").toInt() : 15;
  uint32_t start, count;
  histWindow(mins, start, count);
  uint32_t k = count ? (count + MAX_POINTS - 1) / MAX_POINTS : 1;     // slots per chart point
  uint32_t npts = count ? (count + k - 1) / k : 0;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"t0\":%lld,\"step\":%lu,\"n\":%lu,\"now\":%lld,\"off\":%lld,\"real\":%s,\"series\":[",
           (long long)start * SAMPLE_MS, (unsigned long)(k * SAMPLE_MS), (unsigned long)npts,
           (long long)netNow(), (long long)epochOffsetMs, clockSource ? "true" : "false");
  server.sendContent(buf);

  bool firstSeries = true;
  for (int id = 1; id <= MAX_NODES && npts; id++) {
    if (!nodes[id].seen) continue;
    String chunk;
    chunk.reserve(npts * 8 + 40);
    chunk += firstSeries ? "{\"id\":" : ",{\"id\":";
    chunk += id;
    chunk += ",\"v\":[";
    for (uint32_t j = 0; j < npts; j++) {
      float sum = 0; int cnt = 0;                    // average each bucket
      for (uint32_t s = start + j * k; s < start + (j + 1) * k && s < start + count; s++) {
        const HistCell* c = histGet(id, s);
        if (c) { sum += metricOf(c->d, m); cnt++; }
      }
      if (j) chunk += ',';
      if (cnt) { snprintf(buf, sizeof(buf), "%.2f", sum / cnt); chunk += buf; }
      else chunk += "null";
    }
    chunk += "]}";
    server.sendContent(chunk);
    firstSeries = false;
  }
  server.sendContent("]}");
  server.sendContent("");
}

void handleHistoryCsv() {
  long mins = server.hasArg("min") ? server.arg("min").toInt() : 60;
  uint32_t start, count;
  histWindow(mins, start, count);

  server.sendHeader("Content-Disposition", "attachment; filename=\"lora_history.csv\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent(clockSource ? "time_utc,epoch_ms,node,voltage_V,current_A,power_W,temp_C,status\n"
                                 : "time_since_hub_boot,net_ms,node,voltage_V,current_A,power_W,temp_C,status\n");
  char ts[32], line[160];
  for (uint32_t s = start; s < start + count; s++) {
    String chunk;
    int64_t tNet = (int64_t)s * SAMPLE_MS;
    fmtTime(tNet, ts, sizeof(ts));
    long long tOut = clockSource ? (long long)(tNet + epochOffsetMs) : (long long)tNet;
    for (int id = 1; id <= MAX_NODES; id++) {
      const HistCell* c = histGet(id, s);
      if (!c) continue;
      snprintf(line, sizeof(line), "%s,%lld,%d,%.2f,%.2f,%.1f,%.2f,%u\n", ts, tOut, id,
               c->d.voltage_cV / 100.0, c->d.current_cA / 100.0, c->d.power_dW / 10.0,
               c->d.temp_cC / 100.0, c->d.status);
      chunk += line;
    }
    if (chunk.length()) server.sendContent(chunk);
  }
  server.sendContent("");
}

// Incremental sync for the phone/iPad: raw rows from slot `from` onward.
// Response: {"boot","real","off","step","oldest","newest","next","rewind",
//            "rows":[[slot,[id,cV,cA,dW,cC,status, id,...]],...]}
#define SYNC_SCAN_SLOTS 200
void handleApiSync() {
  uint32_t from = server.hasArg("from") ? (uint32_t)strtoul(server.arg("from").c_str(), nullptr, 10) : 0;
  uint32_t newest = histNewest;
  bool any = histAny && histSlots;
  uint32_t oldest = 0;
  if (any) oldest = (newest + 1 > histSlots) ? newest + 1 - histSlots : 0;
  if (from < oldest) from = oldest;
  uint32_t last = any ? newest : 0;
  if (any && from + SYNC_SCAN_SLOTS - 1 < last) last = from + SYNC_SCAN_SLOTS - 1;
  uint32_t next = (any && from <= last) ? last + 1 : from;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  char buf[220];
  snprintf(buf, sizeof(buf),
           "{\"boot\":%lu,\"real\":%s,\"off\":%lld,\"step\":%d,\"oldest\":%lu,\"newest\":%lu,"
           "\"next\":%lu,\"rewind\":%d,\"rows\":[",
           (unsigned long)hubBootId, clockSource ? "true" : "false", (long long)epochOffsetMs,
           SAMPLE_MS, (unsigned long)oldest, (unsigned long)(any ? newest : 0),
           (unsigned long)next, BACKFILL_WINDOW + 4);
  server.sendContent(buf);

  bool firstRow = true;
  if (any && from <= last) {
    for (uint32_t s = from; s <= last; s++) {
      String row;
      bool has = false;
      for (int id = 1; id <= MAX_NODES; id++) {
        const HistCell* c = histGet(id, s);
        if (!c) continue;
        snprintf(buf, sizeof(buf), "%s%d,%u,%d,%ld,%d,%u", has ? "," : "", id,
                 c->d.voltage_cV, c->d.current_cA, (long)c->d.power_dW, c->d.temp_cC, c->d.status);
        row += buf;
        has = true;
      }
      if (!has) continue;
      snprintf(buf, sizeof(buf), "%s[%lu,[", firstRow ? "" : ",", (unsigned long)s);
      String out = buf;
      out += row;
      out += "]]";
      server.sendContent(out);
      firstRow = false;
    }
  }
  server.sendContent("]}");
  server.sendContent("");
}

void handleApiCmd() {
  String who = server.arg("id");
  String c   = server.arg("c");
  uint8_t cmd = (c == "identify") ? CMD_IDENTIFY : (c == "reboot") ? CMD_REBOOT : CMD_NONE;
  int id = (who == "all") ? CMD_ALL : who.toInt();
  if (cmd == CMD_NONE || (id != CMD_ALL && (id < 1 || id > MAX_NODES))) {
    server.send(400, "text/plain", "bad request");
    return;
  }
  hubQueueCmd((uint8_t)id, cmd, 0);
  server.send(200, "text/plain", "ok");
}

void webTask(void* param) {
  for (;;) {
    server.handleClient();
    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
}

void hubSetup() {
  nodesMutex = xSemaphoreCreateMutex();
  memset(nodes, 0, sizeof(nodes));
  hubBootId = esp_random();
  histInit();

  setCpuFrequencyMhz(HUB_CPU_MHZ);              // less heat; plenty for Wi-Fi + radio
  useSta = strlen(STA_SSID) > 0;
  WiFi.mode(useSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setTxPower(HUB_WIFI_TX_POWER);           // phone/iPad is close by: no need for full power
  Serial.printf("Wi-Fi AP \"%s\" (password %s): open http://%s\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());

  if (useSta) {
    WiFi.begin(STA_SSID, STA_PASS);
    Serial.printf("Joining \"%s\"", STA_SSID);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(250); Serial.print('.'); }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nAlso reachable at http://%s\n", WiFi.localIP().toString().c_str());
      configTime(0, 0, NTP_SERVER);
    } else {
      Serial.println("\nCould not join, AP only.");
    }
  }

  server.on("/", handleRoot);
  server.on("/api/nodes", handleApiNodes);
  server.on("/api/history", handleApiHistory);
  server.on("/api/time", handleApiTime);
  server.on("/api/cmd", handleApiCmd);
  server.on("/api/sync", handleApiSync);
  server.on("/history.csv", handleHistoryCsv);
  server.begin();
  xTaskCreatePinnedToCore(webTask, "web", 8192, NULL, 1, NULL, 0);

  nextCycleUs = esp_timer_get_time() + 500000;   // first cycle in 0.5 s
  Serial.printf("Reply timeout %.1f ms. HUB running. Open the web page to set the clock.\n",
                replyTimeoutUs / 1000.0);
}
