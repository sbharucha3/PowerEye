// Standalone sensor node (no radio): ADC -> OLED + LEDs. Known-good fallback for the demo.
#include <Arduino.h>
#include <PowerEyeSense.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("PowerEye standalone node: ADC -> OLED + LEDs");
  pe::begin();
}

void loop() {
  static uint32_t lastSeq = 0;
  pe::Reading r;
  if (pe::latest(r) && r.seq != lastSeq) {
    lastSeq = r.seq;
    static const char *names[] = {"NO INPUT", "GOOD", "BAD", "TRIP"};
    Serial.printf("%-8s  %.1f mV -> %.1f V | %.1f mV -> %.2f A\n", names[r.state], r.vEvmMv, r.vLine, r.iEvmMv, r.iLine);
  }
  delay(50);
}
