// ============================================================================
//  NODE: follows the hub's clock, samples on the shared time grid, keeps a
//        rolling history, and answers polls with the newest sample plus
//        anything the hub missed
// ============================================================================

Stored   ring[BUFFER_SAMPLES];      // rolling history (oldest is overwritten when full)
uint32_t ringTail = 0;              // index of the oldest sample
uint32_t ringCount = 0;
uint16_t sampleSeq = 0;             // last sample number taken
uint16_t oldestSeq = 1, newestSeq = 0;
uint16_t bootId = 1;

// Network clock: netTime = localMs() + netOffsetMs (set from every poll)
int64_t  netOffsetMs = 0;
bool     netSynced = false;
int64_t  nextSampleNet = 0;

unsigned long pollsHeard = 0, repliesSent = 0, samplesSent = 0, backfillSent = 0;
unsigned long lastPolledMs = 0, lastNotPolledWarnMs = 0;
int8_t   lastPollRssi = 0, lastPollSnr4 = 0;
uint16_t lastBacklog = 0;
uint8_t  lastCmdSeq = 0;
unsigned long identifyUntil = 0;
bool     verbose = true;

int64_t nodeNetNow() { return localMs() + netOffsetMs; }

int64_t nextGridAfter(int64_t t) { return (t / SAMPLE_MS + 1) * SAMPLE_MS; }

// ---------------------------------------------------------------------------
//  Placeholder sensors: REPLACE with your real sensor reads
// ---------------------------------------------------------------------------
void readSensors(SensorData& s) {
  pe::Reading r;
  bool ok = pe::latest(r) && r.adcOk && r.ageMs < 3000;
  float volts = ok ? r.vLine : 0.0f;
  float amps  = ok ? r.iLine : 0.0f;
  s.voltage_cV = (uint16_t)lroundf(constrain(volts, 0.0f, 650.0f) * 100.0f);
  s.current_cA = (int16_t)lroundf(constrain(amps, -300.0f, 300.0f) * 100.0f);
  s.power_dW   = (int32_t)lroundf(volts * amps * 10.0f);
  s.temp_cC    = (int16_t)lroundf(chipTempC * 100.0f);
  s.status     = ok ? pe::statusByte(r) : 0;   // bit0 good, bit1 bad, bit2 trip (see PowerEyeSense.h)
}

// ---------------------------------------------------------------------------
void nodeTakeSample() {
  Stored st;
  st.seq = ++sampleSeq;
  st.tLocalMs = (uint32_t)localMs();
  readSensors(st.d);

  uint32_t idx;
  if (ringCount < BUFFER_SAMPLES) {
    idx = (ringTail + ringCount) % BUFFER_SAMPLES;
    ringCount++;
  } else {                                   // full: overwrite the oldest
    idx = ringTail;
    ringTail = (ringTail + 1) % BUFFER_SAMPLES;
  }
  ring[idx] = st;
  newestSeq = st.seq;
  oldestSeq = (uint16_t)(newestSeq - ringCount + 1);
}

Stored* getStored(uint16_t seq) {
  uint16_t offset = seq - oldestSeq;
  if (offset >= ringCount) return nullptr;
  return &ring[(ringTail + offset) % BUFFER_SAMPLES];
}

bool addSample(Reply& r, uint8_t& n, uint16_t seq) {
  Stored* st = getStored(seq);
  if (!st || n >= MAX_PER_REPLY) return false;
  WireSample w;
  w.seq   = st->seq;
  w.ageMs = (uint32_t)localMs() - st->tLocalMs;
  w.d     = st->d;
  r.smp[n] = w;
  n++;
  return true;
}

void runCommand(uint8_t cmd, int16_t arg) {
  if (cmd == CMD_IDENTIFY) {
    identifyUntil = millis() + (arg > 0 ? arg : 5) * 1000UL;
    Serial.println("[NODE] IDENTIFY: blinking LED");
  } else if (cmd == CMD_REBOOT) {
    if (millis() < 15000) return;            // just rebooted: ignore repeats
    Serial.println("[NODE] REBOOT command received");
    delay(100);
    ESP.restart();
  }
}

// Follow the hub's clock. The poll was stamped just before it went on air,
// so the hub's time "now" is that stamp plus the poll's airtime.
void syncClock(const Poll& p, int64_t rxUs) {
  int64_t hubNowAtRx = p.netMs + (int64_t)(pollToaUs / 1000);
  int64_t newOffset  = hubNowAtRx - rxUs / 1000;
  int64_t jump       = newOffset - netOffsetMs;
  netOffsetMs = newOffset;
  if (!netSynced || llabs(jump) > SAMPLE_MS / 4) {
    netSynced = true;
    nextSampleNet = nextGridAfter(nodeNetNow());   // realign to the shared grid
    Serial.printf("[N%u] clock synced to hub (adjusted %lld ms)\n", nodeId, (long long)jump);
  }
}

void nodeHandlePoll(const Poll& p, float rssi, float snr, int64_t rxUs) {
  syncClock(p, rxUs);
  pollsHeard++;
  lastPolledMs  = millis();
  lastPollRssi  = (int8_t)constrain(rssi, -128.0f, 127.0f);
  lastPollSnr4  = (int8_t)constrain(snr * 4.0f, -128.0f, 127.0f);

  bool fresh = p.flags & POLL_FLAG_FRESH;
  uint16_t haveSeq  = p.haveSeq;
  uint64_t haveMask = p.haveMask;

  // Does the hub already have sample s?
  auto hubHas = [&](uint16_t s) -> bool {
    if (fresh) return false;
    uint16_t d = s - haveSeq;
    if (d == 0 || d >= 32768) return true;   // at or below haveSeq
    d -= 1;
    if (d < 64) return (haveMask >> d) & 1ULL;
    return false;
  };

  Reply r = {};
  r.h.type      = PKT_REPLY;
  r.h.net       = NET_ID;
  r.h.id        = nodeId;
  r.h.bootId    = bootId;
  r.h.newestSeq = newestSeq;
  r.h.buffered  = ringCount > 65535 ? 65535 : (uint16_t)ringCount;
  r.h.pollRssi  = lastPollRssi;
  r.h.pollSnr4  = lastPollSnr4;
  r.h.chipTempC = (int8_t)constrain(chipTempC, -40.0f, 125.0f);
  r.h.radioResets = radioResets;

  uint8_t n = 0, nBackfill = 0;
  lastBacklog = 0;
  if (ringCount > 0) {
    // 1) newest sample first, so the hub's live view never lags
    if (!hubHas(newestSeq)) addSample(r, n, newestSeq);

    // 2) then the oldest samples the hub is missing (also catches the 2nd new
    //    sample when a 5 s cycle spans two 3 s samples)
    if (!fresh) {
      uint16_t start = haveSeq + 1;
      if ((uint16_t)(oldestSeq - start) < 32768) start = oldestSeq;   // already overwritten
      uint16_t span = newestSeq - start;
      if (span < 32768) {
        for (uint16_t k = 0; k < span; k++) {
          uint16_t s = start + k;
          if (hubHas(s)) continue;
          lastBacklog++;
          if (n < MAX_PER_REPLY && addSample(r, n, s)) nBackfill++;
        }
      }
    }
  }
  r.h.count = n;
  if (lastBacklog >= nBackfill) lastBacklog -= nBackfill;

  if (p.cmd != CMD_NONE && p.cmdSeq != lastCmdSeq) {
    lastCmdSeq = p.cmdSeq;
    runCommand(p.cmd, p.cmdArg);
  }

  delay(NODE_REPLY_DELAY_MS);
  if (radioTx(&r, sizeof(ReplyHeader) + n * sizeof(WireSample))) {
    repliesSent++;
    samplesSent += n;
    backfillSent += nBackfill;
    ledFlash(20);
    if (verbose) {
      Serial.printf("[N%u] polled (cycle %u) RSSI %d dBm SNR %.1f | sent %u sample%s (newest #%u) | backlog %u | stored %lu/%d\n",
                    nodeId, p.cycle, lastPollRssi, lastPollSnr4 / 4.0, n, n == 1 ? "" : "s", newestSeq,
                    lastBacklog, (unsigned long)ringCount, BUFFER_SAMPLES);
    }
  }
}

void nodeOnPacket(const uint8_t* buf, int len, float rssi, float snr, int64_t rxUs) {
  if (len != (int)sizeof(Poll)) return;       // other nodes' replies: ignore
  Poll p;
  memcpy(&p, buf, sizeof(p));
  if (p.type != PKT_POLL || p.net != NET_ID) return;
  if (nodeId == 0 || p.id != nodeId) return;  // not for us
  nodeHandlePoll(p, rssi, snr, rxUs);
}

void nodeLoop() {
  pe::setLink((nodeId > 0 && lastPolledMs && millis() - lastPolledMs < 15000) ? pe::LINK_OK : pe::LINK_SEARCH);
  // Sample on the shared grid (every SAMPLE_MS of network time)
  int64_t now = nodeNetNow();
  if (now >= nextSampleNet) {
    nodeTakeSample();
    nextSampleNet += SAMPLE_MS;
    if (nextSampleNet <= now) nextSampleNet = nextGridAfter(now);
  }

  // Warn if the hub hasn't polled us for a while (we keep logging regardless)
  if (nodeId > 0 && millis() - lastPolledMs > 15000 && millis() - lastNotPolledWarnMs > 15000) {
    lastNotPolledWarnMs = millis();
    Serial.printf("[N%u] not polled for %lu s (still logging: %lu samples stored)\n",
                  nodeId, (millis() - lastPolledMs) / 1000, (unsigned long)ringCount);
  }

  // Contingency: a long silence may mean our radio is stuck. Reset it.
  // Harmless if the hub is simply off; logging carries on either way.
  static unsigned long lastReinitMs = 0;
  if (nodeId > 0 && millis() - lastPolledMs > NODE_SILENT_MS_REINIT &&
      millis() - lastReinitMs > NODE_SILENT_MS_REINIT) {
    lastReinitMs = millis();
    radioReinit("not polled for a while");
  }

  // Identify: fast blink
  if (identifyUntil) {
    if (millis() < identifyUntil) {
      digitalWrite(LED_PIN, (millis() / 100) % 2 ? LOW : HIGH);
    } else {
      identifyUntil = 0;
      digitalWrite(LED_PIN, HIGH);
    }
  }
}

bool nodeCommand(String& line) {
  if (line == "stats") {
    Serial.printf("Node %u | clock %s | polls heard %lu | replies %lu | samples sent %lu (%lu backfill) | backlog %u\n",
                  nodeId, netSynced ? "synced" : "NOT synced", pollsHeard, repliesSent,
                  samplesSent, backfillSent, lastBacklog);
    Serial.printf("Stored %lu/%d samples (#%u to #%u) | last poll RSSI %d dBm, SNR %.1f dB\n",
                  (unsigned long)ringCount, BUFFER_SAMPLES, oldestSeq, newestSeq,
                  lastPollRssi, lastPollSnr4 / 4.0);
    return true;
  }
  if (line == "v") {
    verbose = !verbose;
    Serial.printf("Verbose %s\n", verbose ? "ON" : "OFF");
    return true;
  }
  if (line == "dump") {
    // net_ms is the hub's network clock, identical across nodes
    Serial.println("seq,net_ms,age_s,V,A,W,C,status");
    uint32_t nowLocal = (uint32_t)localMs();
    for (uint32_t i = 0; i < ringCount; i++) {
      Stored& st = ring[(ringTail + i) % BUFFER_SAMPLES];
      uint32_t age = nowLocal - st.tLocalMs;
      if ((i & 63) == 0) esp_task_wdt_reset();   // long print: keep the watchdog happy
      Serial.printf("%u,%lld,%.1f,%.2f,%.2f,%.1f,%.2f,%u\n", st.seq,
                    (long long)(nodeNetNow() - age), age / 1000.0,
                    st.d.voltage_cV / 100.0, st.d.current_cA / 100.0, st.d.power_dW / 10.0,
                    st.d.temp_cC / 100.0, st.d.status);
    }
    return true;
  }
  return false;
}

void nodeSetup() {
  pe::begin();   // ADC + OLED + LEDs run on their own task
  bootId = (uint16_t)(esp_random() & 0xFFFF) | 1;
  nextSampleNet = nextGridAfter(nodeNetNow());
  if (nodeId < 1 || nodeId > MAX_NODES) {
    Serial.printf("This node has no ID yet. Type: id <1-%d>\n", MAX_NODES);
  }
  Serial.println("NODE running: logging data and waiting for the hub...");
}
