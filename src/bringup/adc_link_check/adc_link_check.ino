/*
  PowerEye node: ADC readout (XIAO ESP32S3 + ADC 15 Click / ADS131M02)

  Pin map (from the assembly guide):
    D5 = SCK, D6 = MOSI, D7 = MISO
    D0 = CS1 (ADC chip select)
    D4 = CS2 (clock chip select, held HIGH here so it stays off the bus)
    D1 = DRDY (click pin 15 INT, active low)

  Channels:
    CH0 (AIN0) = voltage  (AMC3330EVM)
    CH1 (AIN1) = current  (AMC3301EVM)

  Output (Serial Monitor, 115200 baud):
    - every 1 s : last raw sample + RMS + DC offset for each channel
    - every 10 s: average of the last 10 one-second values

  Board package: "esp32 by Espressif", board "XIAO_ESP32S3".
*/

#include <SPI.h>

// ---------------- pins ----------------
const int PIN_SCK  = D5;
const int PIN_MOSI = D6;
const int PIN_MISO = D7;
const int PIN_CS1  = D0;
const int PIN_CS2  = D4;
const int PIN_DRDY = D1;

// ---------------- scaling (EDIT THESE once the mains side is built) ----------------
// ADS131M02: gain 1, full scale +/-1.2 V over 24-bit signed codes.
const double ADC_VOLTS_PER_COUNT = 1.2 / 8388608.0;
// Converts volts at the ADC input to real-world units. 1.0 = "ADC input volts".
// Depends on your divider + AMC3330 gain (voltage) and shunt + AMC3301 gain (current).
const double V_SCALE = 1.0;   // mains volts per ADC-input volt
const double I_SCALE = 1.0;   // amps per ADC-input volt

// ---------------- ADS131M02 ----------------
const uint16_t CMD_NULL  = 0x0000;
const uint16_t CMD_RESET = 0x0011;
const uint16_t CMD_RREG  = 0xA000;   // | (addr << 7) | (count - 1)

SPIClass adcSPI(FSPI);
SPISettings adcSettings(2000000, MSBFIRST, SPI_MODE1);  // ADC wants SPI mode 1

volatile uint32_t drdyCount = 0;
void IRAM_ATTR onDrdy() { drdyCount = drdyCount + 1; }

static inline int32_t signExt24(uint8_t b0, uint8_t b1, uint8_t b2) {
  int32_t v = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
  if (v & 0x800000) v |= 0xFF000000;
  return v;
}

// One 4-word frame (24-bit words): [cmd/response][CH0][CH1][CRC]
void adcFrame(uint16_t cmd, uint16_t &resp, int32_t &ch0, int32_t &ch1) {
  uint8_t tx[12] = {0};
  uint8_t rx[12] = {0};
  tx[0] = cmd >> 8;
  tx[1] = cmd & 0xFF;
  adcSPI.beginTransaction(adcSettings);
  digitalWrite(PIN_CS1, LOW);
  adcSPI.transferBytes(tx, rx, 12);
  digitalWrite(PIN_CS1, HIGH);
  adcSPI.endTransaction();
  resp = ((uint16_t)rx[0] << 8) | rx[1];
  ch0  = signExt24(rx[3], rx[4], rx[5]);
  ch1  = signExt24(rx[6], rx[7], rx[8]);
}

uint16_t adcReadReg(uint8_t addr) {
  uint16_t r; int32_t a, b;
  adcFrame(CMD_RREG | ((uint16_t)addr << 7), r, a, b);  // send the read command
  adcFrame(CMD_NULL, r, a, b);                           // register value comes back next frame
  return r;
}

// ---------------- accumulation ----------------
const int HIST = 10;                 // seconds of history kept
double histVrms[HIST], histIrms[HIST], histVdc[HIST], histIdc[HIST];
int histIdx = 0, histCount = 0;

double sumV = 0, sumI = 0, sumV2 = 0, sumI2 = 0;
uint32_t nSamples = 0;
int32_t lastV = 0, lastI = 0;

uint32_t secStart = 0;
uint32_t secondsRunning = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(PIN_CS1, OUTPUT);  digitalWrite(PIN_CS1, HIGH);
  pinMode(PIN_CS2, OUTPUT);  digitalWrite(PIN_CS2, HIGH);   // clock chip deselected
  pinMode(PIN_DRDY, INPUT_PULLUP);

  adcSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);

  uint16_t r; int32_t a, b;
  adcFrame(CMD_RESET, r, a, b);
  delay(5);
  adcFrame(CMD_NULL, r, a, b);
  Serial.printf("Reset response: 0x%04X (datasheet value for the 2-channel part should be 0xFF22)\n", r);
  Serial.printf("ID register:    0x%04X (0x0000 or 0xFFFF means no SPI link)\n", adcReadReg(0x00));

  attachInterrupt(digitalPinToInterrupt(PIN_DRDY), onDrdy, FALLING);

  Serial.println("time_s,V_raw,I_raw,V_rms_raw,I_rms_raw,V_dc_raw,I_dc_raw,V_rms,I_rms");
  secStart = millis();
}

void loop() {
  // Read one frame per DRDY pulse
  static uint32_t handled = 0;
  while (handled != drdyCount) {
    handled = handled + 1;
    uint16_t r; int32_t v, i;
    adcFrame(CMD_NULL, r, v, i);
    lastV = v; lastI = i;
    sumV += v;  sumV2 += (double)v * v;
    sumI += i;  sumI2 += (double)i * i;
    nSamples++;
  }

  // One-second boundary
  if (millis() - secStart >= 1000) {
    secStart += 1000;
    secondsRunning++;

    if (nSamples == 0) {
      Serial.println("No samples this second: check DRDY wiring (D1 <- click pin 15) and click power.");
      return;
    }

    double vMean = sumV / nSamples, iMean = sumI / nSamples;
    // RMS of the AC part (subtract DC offset)
    double vRms = sqrt(max(0.0, sumV2 / nSamples - vMean * vMean));
    double iRms = sqrt(max(0.0, sumI2 / nSamples - iMean * iMean));

    histVrms[histIdx] = vRms; histIrms[histIdx] = iRms;
    histVdc[histIdx]  = vMean; histIdc[histIdx] = iMean;
    histIdx = (histIdx + 1) % HIST;
    if (histCount < HIST) histCount++;

    Serial.printf("%lu,%ld,%ld,%.1f,%.1f,%.1f,%.1f,%.4f,%.4f   (%lu samples)\n",
                  (unsigned long)secondsRunning, (long)lastV, (long)lastI,
                  vRms, iRms, vMean, iMean,
                  vRms * ADC_VOLTS_PER_COUNT * V_SCALE,
                  iRms * ADC_VOLTS_PER_COUNT * I_SCALE,
                  (unsigned long)nSamples);

    sumV = sumI = sumV2 = sumI2 = 0; nSamples = 0;

    // Every 10 seconds: average of the stored one-second values
    if (secondsRunning % 10 == 0 && histCount == HIST) {
      double aVr = 0, aIr = 0, aVd = 0, aId = 0;
      for (int k = 0; k < HIST; k++) {
        aVr += histVrms[k]; aIr += histIrms[k];
        aVd += histVdc[k];  aId += histIdc[k];
      }
      aVr /= HIST; aIr /= HIST; aVd /= HIST; aId /= HIST;
      Serial.printf(">>> 10 s AVERAGE @ t=%lu s: V_rms_raw=%.1f I_rms_raw=%.1f V_dc_raw=%.1f I_dc_raw=%.1f | V_rms=%.4f I_rms=%.4f\n",
                    (unsigned long)secondsRunning, aVr, aIr, aVd, aId,
                    aVr * ADC_VOLTS_PER_COUNT * V_SCALE,
                    aIr * ADC_VOLTS_PER_COUNT * I_SCALE);
    }
  }
}
