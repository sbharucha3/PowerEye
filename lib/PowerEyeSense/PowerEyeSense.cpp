#include "PowerEyeSense.h"
#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

namespace pe {
namespace {

// ---------- pins (XIAO ESP32-S3) ----------
const int PIN_SCK = D5, PIN_MISO = D7, PIN_MOSI = D6;
const int PIN_CS_ADC = D0, PIN_CS_CLK = D4, PIN_DRDY = D1;
const int SDA_PIN = 3, SCL_PIN = 4;            // D2, D3
const uint8_t PCA_ADDR = 0x40;

// ---------- LED wiring on the PCA9685 (change here if the LEDs sit on other channels) ----------
const uint8_t LED1_RED = 0, LED1_GREEN = 1;    // LED 1 = signal state
const uint8_t LED2_RED = 2, LED2_GREEN = 3;    // LED 2 = link state
const uint16_t LED_DUTY = 500;                 // ~12% of 4095: bright enough indoors, light on the battery

// ---------- scaling (verified on the bench 2026-11-11) ----------
const double LSB_V = 1.2 / 8388608.0;          // ADS131M02 gain 1
const double GAIN_V = 2.0, GAIN_I = 8.2;       // AMC3330 / AMC3301
const double DIV_RATIO = 499.0;                // mains-build divider (996k + 2k) / 2k
const double SHUNT_OHM = 0.002;

// ---------- signal limits ----------
const float V_LO = 108.0f, V_HI = 132.0f;      // good band: 120 V +/- 10%
const float V_TRIP = 140.0f;
const float I_MAX = 15.0f;                     // above this: bad (15 A design limit)
const float I_MAX_CLEAR = 14.7f;               // hysteresis: must fall below this to go good again
const float I_TRIP = 20.0f;                    // above this: trip
const uint32_t TRIP_HOLD_MS = 5000;
const double NO_INPUT_EVM_V = 0.020;           // below 20 mV RMS at the voltage EVM = nothing connected

const int N_SAMPLES = 2400;                    // 0.6 s at 4 kSPS = 36 cycles of 60 Hz

SPIClass adcSpi(HSPI);                         // the radio owns the global SPI (FSPI)
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

TaskHandle_t taskH = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Reading shared = {};
uint32_t sharedMs = 0;
volatile LinkState link = LINK_NONE;
volatile bool adcOk = false;
uint32_t tripUntil = 0;
bool started = false;

void IRAM_ATTR onDrdy() {
  BaseType_t hp = pdFALSE;
  if (taskH) vTaskNotifyGiveFromISR(taskH, &hp);
  if (hp) portYIELD_FROM_ISR();
}

// ---------- PCA9685 ----------
void pcaWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCA_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
void pcaInit() {
  pcaWrite(0x00, 0x10);
  pcaWrite(0xFE, (uint8_t)(25000000.0f / (4096.0f * 1000.0f) - 1.0f + 0.5f));  // 1 kHz PWM
  pcaWrite(0x01, 0x04);
  pcaWrite(0x00, 0x20);
  delay(1);
  pcaWrite(0x00, 0xA0);
}
void ledSet(uint8_t ch, uint16_t duty) {
  uint16_t on = 0, off = duty;
  if (duty == 0) { on = 0; off = 0x1000; }
  else if (duty >= 4095) { on = 0x1000; off = 0; }
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(0x06 + 4 * ch);
  Wire.write(on & 0xFF); Wire.write(on >> 8); Wire.write(off & 0xFF); Wire.write(off >> 8);
  Wire.endTransmission();
}

// bi-color LED helper: r/g = 0 or 1
uint8_t cache[4] = {255, 255, 255, 255};
void setLed(bool led2, bool r, bool g) {
  uint8_t rc = led2 ? LED2_RED : LED1_RED, gc = led2 ? LED2_GREEN : LED1_GREEN;
  uint8_t k = led2 ? 2 : 0;
  if (cache[k] != r) { ledSet(rc, r ? LED_DUTY : 0); cache[k] = r; }
  if (cache[k + 1] != g) { ledSet(gc, g ? LED_DUTY : 0); cache[k + 1] = g; }
}

// Called often from the sample loop; only touches the bus when a LED actually changes.
void ledTick(SigState s, bool fault) {
  uint32_t t = millis();
  bool phase = (t / 125) & 1;          // trip/fault alternate every 125 ms (8 Hz)
  // LED 1: signal state. off = no input, green = good, red = bad, alternating red/green = trip or ADC fault
  if (fault || s == SIG_TRIP) setLed(false, phase, !phase);
  else if (s == SIG_GOOD)     setLed(false, false, true);
  else if (s == SIG_BAD)      setLed(false, true, false);
  else                        setLed(false, false, false);
  // LED 2: link state. steady green = linked (or standalone build), orange blink = searching, red = radio fault
  switch (link) {
    case LINK_OK:     setLed(true, false, true); break;
    case LINK_SEARCH: setLed(true, (t / 500) & 1, (t / 500) & 1); break;
    case LINK_FAULT:  setLed(true, true, false); break;
    default:          setLed(true, false, true); break;   // LINK_NONE (standalone): steady green = powered
  }
}

// ---------- ADS131M02 / LTC6903 ----------
uint16_t ltcWord(double f) {
  int oct = (int)floor(log2(f / 1039.0)); if (oct < 0) oct = 0; if (oct > 15) oct = 15;
  int dac = (int)lround(2048.0 - 2078.0 * pow(2.0, 10 + oct) / f);
  if (dac < 0) dac = 0; if (dac > 1023) dac = 1023;
  return (uint16_t)((oct << 12) | (dac << 2));
}
void ltcProgram(uint16_t w) {
  adcSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS_CLK, LOW); delayMicroseconds(2);
  adcSpi.transfer16(w);
  delayMicroseconds(2); digitalWrite(PIN_CS_CLK, HIGH);
  adcSpi.endTransaction();
}
void adcFrame(uint16_t cmd, uint8_t *rx) {
  uint8_t tx[12] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  adcSpi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE1));
  digitalWrite(PIN_CS_ADC, LOW); delayMicroseconds(1);
  adcSpi.transferBytes(tx, rx, 12);
  delayMicroseconds(1); digitalWrite(PIN_CS_ADC, HIGH);
  adcSpi.endTransaction();
}
int32_t s24(const uint8_t *p) {
  int32_t v = ((int32_t)p[0] << 16) | ((int32_t)p[1] << 8) | p[2];
  return (v & 0x800000) ? (v | 0xFF000000) : v;
}
uint16_t adcReadReg(uint8_t a) {
  uint8_t rx[12];
  adcFrame(0xA000 | ((uint16_t)a << 7), rx);
  adcFrame(0x0000, rx);
  return ((uint16_t)rx[0] << 8) | rx[1];
}
bool adcInit() {
  ltcProgram(ltcWord(8192000.0));
  delay(20);
  uint8_t rx[12];
  adcFrame(0x0011, rx);                 // RESET
  delay(10);
  adcFrame(0x0000, rx);
  uint16_t reply = ((uint16_t)rx[0] << 8) | rx[1];
  uint16_t id = adcReadReg(0x00);
  return reply == 0xFF22 && (id >> 8) == 0x22;
}

// ---------- OLED ----------
void drawRows(const Reading &r, bool fault) {
  char top[24], bot[24], vs[16], is[16];
  const char *vStat, *iStat;
  if (fault) {
    snprintf(top, sizeof top, "ADC FAULT"); snprintf(bot, sizeof bot, "check wiring");
    vStat = "--"; iStat = "--";
  } else if (r.state == SIG_NO_INPUT) {
    snprintf(top, sizeof top, "--.-mV -> --.-V");
    snprintf(bot, sizeof bot, "--.-mV -> --.--A");
    vStat = "NO INPUT"; iStat = "NO INPUT";
  } else {
    snprintf(vs, sizeof vs, r.vEvmMv < 1000 ? "%.1fmV" : "%.0fmV", r.vEvmMv);
    snprintf(is, sizeof is, "%.1fmV", r.iEvmMv);
    snprintf(top, sizeof top, "%s -> %.1fV", vs, r.vLine);
    snprintf(bot, sizeof bot, "%s -> %.2fA", is, r.iLine);
    vStat = (r.vLine >= V_LO && r.vLine <= V_HI) ? "OK" : (r.vLine > V_TRIP ? "TRIP" : "OUT OF RANGE");
    iStat = r.iLine > I_TRIP ? "TRIP" : (r.iLine > I_MAX_CLEAR ? (r.iLine > I_MAX ? "HIGH" : "OK") : "OK");
  }
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tr);
  oled.drawStr(0, 8, "VOLTAGE");
  oled.drawStr(128 - oled.getStrWidth(vStat), 8, vStat);
  oled.drawStr(0, 41, "CURRENT");
  oled.drawStr(128 - oled.getStrWidth(iStat), 41, iStat);
  oled.drawHLine(0, 31, 128);
  oled.setFont(u8g2_font_7x13B_tr);
  oled.drawStr(0, 23, top);
  oled.drawStr(0, 56, bot);
  oled.sendBuffer();
}

void splash() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_10x20_tr);
  oled.drawStr(18, 28, "PowerEye");
  oled.setFont(u8g2_font_5x8_tr);
  oled.drawStr(30, 46, "starting sensors...");
  oled.sendBuffer();
}

// ---------- main sense task ----------
void senseTask(void *) {
  Wire.setPins(SDA_PIN, SCL_PIN);
  oled.begin();
  Wire.setClock(400000);
  oled.setBusClock(400000);
  pcaInit();
  for (uint8_t i = 0; i < 4; i++) ledSet(i, 0);
  splash();
  { // boot self-test: LED1 red, LED1 green, LED2 red, LED2 green (shows wiring is right)
    const uint8_t seq[4] = {LED1_RED, LED1_GREEN, LED2_RED, LED2_GREEN};
    for (uint8_t i = 0; i < 4; i++) { ledSet(seq[i], LED_DUTY); delay(300); ledSet(seq[i], 0); }
  }

  pinMode(PIN_CS_ADC, OUTPUT); digitalWrite(PIN_CS_ADC, HIGH);
  pinMode(PIN_CS_CLK, OUTPUT); digitalWrite(PIN_CS_CLK, HIGH);
  pinMode(PIN_DRDY, INPUT_PULLUP);
  adcSpi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  adcOk = adcInit();
  attachInterrupt(digitalPinToInterrupt(PIN_DRDY), onDrdy, FALLING);

  Reading cur = {};
  uint32_t seq = 0;
  for (;;) {
    if (!adcOk) {                                   // retry every 2 s, show the fault meanwhile
      cur.adcOk = false; cur.state = SIG_NO_INPUT;
      drawRows(cur, true);
      for (int i = 0; i < 20; i++) { ledTick(SIG_NO_INPUT, true); vTaskDelay(pdMS_TO_TICKS(100)); }
      adcOk = adcInit();
      continue;
    }
    double s0 = 0, q0 = 0, s1 = 0, q1 = 0;
    int32_t mn0 = INT32_MAX, mx0 = INT32_MIN, mn1 = INT32_MAX, mx1 = INT32_MIN;
    int n = 0, misses = 0;
    uint8_t rx[12];
    while (n < N_SAMPLES) {
      if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30))) {
        ledTick(cur.state, false);
        if (++misses > 5) { adcOk = false; break; }
        continue;
      }
      adcFrame(0x0000, rx);
      int32_t c0 = s24(rx + 3), c1 = s24(rx + 6);
      s0 += c0; q0 += (double)c0 * c0; s1 += c1; q1 += (double)c1 * c1;
      if (c0 < mn0) mn0 = c0; if (c0 > mx0) mx0 = c0;
      if (c1 < mn1) mn1 = c1; if (c1 > mx1) mx1 = c1;
      n++;
      if ((n & 63) == 0) ledTick(cur.state, false);
    }
    if (n < N_SAMPLES) continue;

    double m0 = s0 / n;
    double rms0 = sqrt(fmax(0.0, q0 / n - m0 * m0)) * LSB_V;
    double rms1 = sqrt(fmax(0.0, q1 / n - (s1 / n) * (s1 / n))) * LSB_V;
    double pp0 = (mx0 - mn0) * LSB_V;
    bool clip = (mx0 > 8200000 || mn0 < -8200000 || mx1 > 8200000 || mn1 < -8200000);

    double vEvm = rms0 / GAIN_V, iEvm = rms1 / GAIN_I;
    bool noInput = (vEvm < NO_INPUT_EVM_V) || (fabs(m0 * LSB_V) > 1.0 && pp0 < 0.02);
    float vLine = (float)(vEvm * DIV_RATIO), iLine = (float)(iEvm / SHUNT_OHM);

    static bool iHigh = false;                      // hysteresis so 15.0 A does not flicker good/bad
    if (iLine > I_MAX) iHigh = true; else if (iLine < I_MAX_CLEAR) iHigh = false;
    uint32_t now = millis();
    SigState st;
    if (!noInput && (iLine > I_TRIP || vLine > V_TRIP)) tripUntil = now + TRIP_HOLD_MS;
    if ((int32_t)(tripUntil - now) > 0)  st = SIG_TRIP;
    else if (noInput)                    st = SIG_NO_INPUT;
    else if (!clip && vLine >= V_LO && vLine <= V_HI && !iHigh) st = SIG_GOOD;
    else                                 st = SIG_BAD;

    cur.valid = true; cur.adcOk = true; cur.state = st;
    cur.vEvmMv = (float)(vEvm * 1e3); cur.iEvmMv = (float)(iEvm * 1e3);
    cur.vLine = noInput ? 0 : vLine;  cur.iLine = noInput ? 0 : iLine;
    if (noInput) { cur.vEvmMv = 0; cur.iEvmMv = 0; }
    cur.seq = ++seq;

    portENTER_CRITICAL(&mux);
    shared = cur; sharedMs = now;
    portEXIT_CRITICAL(&mux);

    ledTick(st, false);
    drawRows(cur, false);
  }
}

}  // namespace

void begin() {
  if (started) return;
  started = true;
  xTaskCreatePinnedToCore(senseTask, "pe_sense", 8192, nullptr, 2, &taskH, 0);
}

bool latest(Reading &out) {
  portENTER_CRITICAL(&mux);
  out = shared;
  uint32_t t = sharedMs;
  portEXIT_CRITICAL(&mux);
  out.ageMs = millis() - t;
  return out.valid;
}

void setLink(LinkState s) { link = s; }

uint8_t statusByte(const Reading &r) {
  if (!r.valid) return 0;
  switch (r.state) {
    case SIG_GOOD: return ST_GOOD;
    case SIG_BAD:  return ST_BAD;
    case SIG_TRIP: return ST_TRIP;
    default:       return 0;
  }
}

}  // namespace pe
