#pragma once
// PowerEye sensing module: ADS131M02 ADC + SSD1309 OLED + PCA9685 bi-color LEDs.
// One FreeRTOS task on core 0 owns the ADC, the I2C bus, the OLED and the LEDs, so the
// radio code on core 1 never blocks on them. Call pe::begin() once, then read pe::latest().
#include <Arduino.h>

namespace pe {

enum SigState : uint8_t { SIG_NO_INPUT = 0, SIG_GOOD, SIG_BAD, SIG_TRIP };
enum LinkState : uint8_t { LINK_NONE = 0, LINK_SEARCH, LINK_OK, LINK_FAULT };

// Bits of SensorData.status sent to the hub (the dashboard decodes the same bits).
enum : uint8_t { ST_GOOD = 1, ST_BAD = 2, ST_TRIP = 4, ST_BATT = 8 /* reserved: no battery sense wired */ };

struct Reading {
  bool     valid;      // false until the first full measurement window
  bool     adcOk;      // false while the ADC is not answering
  SigState state;
  float    vEvmMv;     // measured RMS at the AMC3330EVM input (mV)
  float    iEvmMv;     // measured RMS at the AMC3301EVM input (mV)
  float    vLine;      // line-equivalent volts RMS  (EVM input x 499, mains-build divider)
  float    iLine;      // line-equivalent amps RMS   (EVM input / 2 mOhm shunt)
  uint32_t seq;        // increments every window
  uint32_t ageMs;      // time since this reading was made
};

void begin();                       // start the sense task (safe to call once)
bool latest(Reading &out);          // copy of the newest reading; false if none yet
void setLink(LinkState s);          // drives LED 2
uint8_t statusByte(const Reading &r);

}  // namespace pe
