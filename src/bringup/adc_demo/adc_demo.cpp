/*
  PowerEye node: ADC demo (XIAO ESP32-S3 + ADC 15 Click / ADS131M02)

  Reads both channels while a wave generator drives the AMC3330EVM (voltage) and
  AMC3301EVM (current) inputs. LOW-VOLTAGE BENCH ONLY: no mains.

  Wiring (v2 assembly guide, steps 6 and 7):
    D5 -> click SCK      D6 -> click SDI (MOSI)   D7 <- click SDO (MISO)
    D0 -> click CS1 (ADC)   D4 -> click pin 16 CS2 (LTC6903 clock chip)
    D1 <- click pin 15 INT (DRDY)
    click RST is tied to 3V3, so the ADC is reset with the SPI RESET command.
  D6/D7 are GPIO43/44 (UART0), so USB CDC On Boot must be Enabled (Serial = USB).

  Arduino IDE: Board "XIAO_ESP32S3", USB CDC On Boot: Enabled. No extra libraries.
  PlatformIO: board = seeed_xiao_esp32s3, build_flags = -DARDUINO_USB_CDC_ON_BOOT=1

  What it does
    1. Programs the LTC6903 clock chip to about 8.192 MHz (SPI mode 0). It powers up at
       about 1 kHz, which is useless to the ADC.
    2. Resets the ADS131M02 (SPI mode 1), checks the 0xFF22 reset reply and the ID register.
    3. On every DRDY it reads one 3-word frame (status, ch0, ch1), gain 1, +/-1.2 V full scale.
       Default OSR 1024 at 8.192 MHz gives about 4 kSPS.
    4. Every 2400 samples (0.6 s = 36 cycles of 60 Hz) it prints RMS, DC and peak values.

  Serial commands (115200, any line ending):  i = ID/STATUS/CLOCK registers   r = re-initialise

  Expected readings for the demo levels (generator High-Z):
    Ch1 240 mVrms into AMC3330EVM -> ADC 481 mV rms, line equivalent about 120 V rms
    Ch2  30 mVrms into AMC3301EVM -> ADC 246 mV rms, current equivalent about 15 A rms
  The 1 k + 10 nF input filters and the divider/shunt tolerances add roughly a percent of error.
  Not verified on hardware: LTC6903 command word and ADS131M02 frame details come from the
  datasheets in datasheets/; if ID or reset checks fail, read the printed values first.
*/

#include <Arduino.h>
#include <SPI.h>

// ---------- pins (XIAO D-numbers) ----------
static const int PIN_SCK    = D5;
static const int PIN_MISO   = D7;
static const int PIN_MOSI   = D6;
static const int PIN_CS_ADC = D0;
static const int PIN_CS_CLK = D4;
static const int PIN_DRDY   = D1;

// ---------- settings ----------
static const double   CLK_TARGET_HZ = 8192000.0;
static const uint32_t SPI_ADC_HZ    = 2000000;     // conservative for jumper wires
static const uint32_t SPI_CLK_HZ    = 1000000;
static const int      N_SAMPLES     = 2400;        // 0.6 s at 4 kSPS = 36 cycles of 60 Hz

// ---------- scaling ----------
static const double VREF      = 1.2;               // ADS131M02 internal reference
static const double LSB_V     = VREF / 8388608.0;  // gain 1: 1.2 V / 2^23 per code
static const double GAIN_V    = 2.0;               // AMC3330 gain
static const double GAIN_I    = 8.2;               // AMC3301 gain
static const double DIV_RATIO = 499.0;             // (4 x 249 k + 2 k) / 2 k, for the mains build
static const double SHUNT_OHM = 0.002;             // 2 mOhm, for the mains build

SPIClass adcSpi(FSPI);

// ---------- LTC6903 ----------
// f = 2^OCT * 2078 Hz / (2 - DAC/1024); word = OCT<<12 | DAC<<2 | CNF (CNF 0 = both outputs on)
uint16_t ltcWord(double f, double &actual) {
  int oct = (int)floor(log2(f / 1039.0));
  if (oct < 0) oct = 0;
  if (oct > 15) oct = 15;
  int dac = (int)lround(2048.0 - 2078.0 * pow(2.0, 10 + oct) / f);
  if (dac < 0) dac = 0;
  if (dac > 1023) dac = 1023;
  actual = pow(2.0, oct) * 2078.0 / (2.0 - dac / 1024.0);
  return (uint16_t)((oct << 12) | (dac << 2));
}

void ltcProgram(uint16_t word) {
  adcSpi.beginTransaction(SPISettings(SPI_CLK_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS_CLK, LOW);
  delayMicroseconds(2);
  adcSpi.transfer16(word);
  delayMicroseconds(2);
  digitalWrite(PIN_CS_CLK, HIGH);
  adcSpi.endTransaction();
}

// ---------- ADS131M02: one 4-word x 24-bit frame (status/cmd, ch0, ch1, CRC) ----------
void adcFrame(uint16_t cmd, uint8_t *rx) {
  uint8_t tx[12] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  adcSpi.beginTransaction(SPISettings(SPI_ADC_HZ, MSBFIRST, SPI_MODE1));
  digitalWrite(PIN_CS_ADC, LOW);
  delayMicroseconds(1);
  adcSpi.transferBytes(tx, rx, 12);
  delayMicroseconds(1);
  digitalWrite(PIN_CS_ADC, HIGH);
  adcSpi.endTransaction();
}

int32_t s24(const uint8_t *p) {
  int32_t v = ((int32_t)p[0] << 16) | ((int32_t)p[1] << 8) | p[2];
  return (v & 0x800000) ? (v | 0xFF000000) : v;
}

uint16_t adcReadReg(uint8_t addr) {
  uint8_t rx[12];
  adcFrame(0xA000 | ((uint16_t)addr << 7), rx);   // RREG, 1 register
  adcFrame(0x0000, rx);                            // response arrives in the next frame
  return ((uint16_t)rx[0] << 8) | rx[1];
}

bool waitDrdy(uint32_t timeout_us) {
  uint32_t t0 = micros();
  while (digitalRead(PIN_DRDY) != LOW) {
    if (micros() - t0 > timeout_us) return false;
  }
  return true;
}

void printRegs() {
  Serial.printf("  ID     = 0x%04X  (expect 0x22xx for ADS131M02)\n", adcReadReg(0x00));
  Serial.printf("  STATUS = 0x%04X\n", adcReadReg(0x01));
  Serial.printf("  MODE   = 0x%04X  (reset default 0x0510)\n", adcReadReg(0x02));
  Serial.printf("  CLOCK  = 0x%04X  (reset default 0x0F0E: both channels on, OSR 1024)\n", adcReadReg(0x03));
}

bool adcInit() {
  Serial.println("\n== init ==");
  double actual;
  uint16_t w = ltcWord(CLK_TARGET_HZ, actual);
  Serial.printf("LTC6903 word 0x%04X -> about %.0f Hz (target %.0f)\n", w, actual, CLK_TARGET_HZ);
  ltcProgram(w);
  delay(20);

  uint8_t rx[12];
  adcFrame(0x0011, rx);                            // RESET
  delay(10);
  adcFrame(0x0000, rx);                            // reply to RESET
  uint16_t reply = ((uint16_t)rx[0] << 8) | rx[1];
  Serial.printf("RESET reply 0x%04X (expect 0xFF22): %s\n", reply, reply == 0xFF22 ? "OK" : "MISMATCH");
  uint16_t id = adcReadReg(0x00);
  bool ok = (reply == 0xFF22) && ((id >> 8) == 0x22);
  printRegs();

  bool drdy = waitDrdy(50000);
  Serial.printf("DRDY toggling: %s\n", drdy ? "yes" : "NO (check D1 wire, clock chip)");
  Serial.println(ok && drdy ? "init OK" : "init has problems, see above");
  return ok && drdy;
}

// ---------- measurement ----------
void measure() {
  double s0 = 0, q0 = 0, s1 = 0, q1 = 0;
  int32_t mn0 = INT32_MAX, mx0 = INT32_MIN, mn1 = INT32_MAX, mx1 = INT32_MIN;
  int n = 0, missed = 0;
  uint8_t rx[12];
  while (n < N_SAMPLES) {
    if (!waitDrdy(20000)) { if (++missed > 5) break; continue; }
    adcFrame(0x0000, rx);
    int32_t c0 = s24(rx + 3), c1 = s24(rx + 6);
    s0 += c0; q0 += (double)c0 * c0; s1 += c1; q1 += (double)c1 * c1;
    if (c0 < mn0) mn0 = c0; if (c0 > mx0) mx0 = c0;
    if (c1 < mn1) mn1 = c1; if (c1 > mx1) mx1 = c1;
    n++;
  }
  if (n < N_SAMPLES) { Serial.printf("only %d samples, DRDY stopped (press r)\n", n); return; }

  double m0 = s0 / n, m1 = s1 / n;
  double rms0 = sqrt(max(0.0, q0 / n - m0 * m0)) * LSB_V;   // ADC-side volts, DC removed
  double rms1 = sqrt(max(0.0, q1 / n - m1 * m1)) * LSB_V;
  bool clip0 = (mx0 > 8200000 || mn0 < -8200000);
  bool clip1 = (mx1 > 8200000 || mn1 < -8200000);

  Serial.printf("V: ADC %7.1f mV rms | EVM in %6.1f mV rms | line-equiv %6.1f V rms | DC %6.2f mV | pk-pk %6.1f mV %s\n",
                rms0 * 1e3, rms0 / GAIN_V * 1e3, rms0 / GAIN_V * DIV_RATIO, m0 * LSB_V * 1e3,
                (mx0 - mn0) * LSB_V * 1e3, clip0 ? "CLIPPING" : "");
  Serial.printf("I: ADC %7.1f mV rms | EVM in %6.1f mV rms | current-equiv %5.2f A rms | DC %6.2f mV | pk-pk %6.1f mV %s\n",
                rms1 * 1e3, rms1 / GAIN_I * 1e3, rms1 / GAIN_I / SHUNT_OHM, m1 * LSB_V * 1e3,
                (mx1 - mn1) * LSB_V * 1e3, clip1 ? "CLIPPING" : "");
}

bool ready = false;

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  pinMode(PIN_CS_ADC, OUTPUT); digitalWrite(PIN_CS_ADC, HIGH);
  pinMode(PIN_CS_CLK, OUTPUT); digitalWrite(PIN_CS_CLK, HIGH);
  pinMode(PIN_DRDY, INPUT_PULLUP);
  adcSpi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  Serial.println("\nPowerEye ADC demo (low voltage only). Commands: i = registers, r = re-init");
  ready = adcInit();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'i') printRegs();
    if (c == 'r') ready = adcInit();
  }
  if (ready) measure();
  else delay(200);
}

