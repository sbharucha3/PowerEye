/*
  PowerEye node: I2C peripheral bring-up (XIAO ESP32-S3)

  Tests: I2C scan, SSD1309 OLED, PCA9685 + bi-color LED.

  Wiring (guide steps 12 and 13):
    D2 = GPIO3 = SDA, D3 = GPIO4 = SCL, both modules on 3V3.
    LED: red anode -> 330 ohm -> PCA9685 channel 0
         green anode -> 330 ohm -> PCA9685 channel 1
         common cathode -> GND

  Arduino IDE setup:
    Board:               XIAO_ESP32S3
    USB CDC On Boot:     Enabled   (needed: plain Serial is UART0 = GPIO43/44, which are your SPI lines)
    Library:             U8g2 (olikraus), via Library Manager
  PlatformIO equivalent:
    board = seeed_xiao_esp32s3
    lib_deps = olikraus/U8g2
    build_flags = -DARDUINO_USB_CDC_ON_BOOT=1

  Serial commands (115200 baud, any line ending):
    s  I2C scan             o  OLED test            l  LED test (red, green, both, fade)
    r  red   g  green   y  both   x  LEDs off       d  PCA9685 register dump
    a  run everything   ?  help
  LED 1 = PCA9685 channels 0 (red) and 1 (green). LED 2 (optional) = channels 2 and 3.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "saver.h"

// ---------- configuration ----------
static const int      SDA_PIN  = 3;        // D2
static const int      SCL_PIN  = 4;        // D3
static const uint32_t I2C_HZ   = 100000;   // start slow; try 400000 later
static const uint8_t  OLED_ADDR = 0x3C;
static const uint8_t  PCA_ADDR  = 0x40;
static const uint8_t  LED_RED    = 0;      // LED 1: PCA9685 channel for the red anode
static const uint8_t  LED_GREEN  = 1;      // LED 1: green anode
static const uint8_t  LED2_RED   = 2;      // LED 2 (optional): red anode
static const uint8_t  LED2_GREEN = 3;      // LED 2 (optional): green anode

// SSD1309 128x64, full buffer, hardware I2C. If your OLED stays blank, try
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C instead, or the 0x3D address below.
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// ---------- I2C helpers ----------
bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void i2cScan() {
  Serial.println("\n== I2C scan ==");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    if (!i2cPresent(a)) continue;
    found++;
    Serial.printf("  0x%02X", a);
    if (a == OLED_ADDR || a == 0x3D) Serial.print("  (OLED)");
    if (a == PCA_ADDR)               Serial.print("  (PCA9685)");
    if (a == 0x70)                   Serial.print("  (PCA9685 all-call)");
    Serial.println();
  }
  if (!found) Serial.println("  nothing answered: check SDA/SCL wires, 3V3 and GND");
  Serial.printf("  OLED 0x%02X: %s\n", OLED_ADDR, i2cPresent(OLED_ADDR) ? "OK" : "MISSING");
  Serial.printf("  PCA9685 0x%02X: %s\n", PCA_ADDR, i2cPresent(PCA_ADDR) ? "OK" : "MISSING");
}

// ---------- PCA9685 (raw registers, no library) ----------
void pcaWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void pcaInit(float freqHz) {
  pcaWrite(0x00, 0x10);                                   // MODE1: sleep, so PRE_SCALE can be set
  uint8_t pre = (uint8_t)(25000000.0f / (4096.0f * freqHz) - 1.0f + 0.5f);
  pcaWrite(0xFE, pre);                                    // PRE_SCALE
  pcaWrite(0x01, 0x04);                                   // MODE2: totem-pole outputs
  pcaWrite(0x00, 0x20);                                   // MODE1: wake, auto-increment
  delay(1);
  pcaWrite(0x00, 0xA0);                                   // MODE1: restart + auto-increment
}

void pcaSetPWM(uint8_t ch, uint16_t on, uint16_t off) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(0x06 + 4 * ch);                              // LEDn_ON_L
  Wire.write(on & 0xFF);  Wire.write(on >> 8);
  Wire.write(off & 0xFF); Wire.write(off >> 8);
  Wire.endTransmission();
}

// duty 0..4095
void ledSet(uint8_t ch, uint16_t duty) {
  if (duty == 0)         pcaSetPWM(ch, 0, 0x1000);        // full off
  else if (duty >= 4095) pcaSetPWM(ch, 0x1000, 0);        // full on
  else                   pcaSetPWM(ch, 0, duty);
}

// brightness 0.0..1.0 with a gamma curve so fades look even
uint16_t gammaDuty(float b) { return (uint16_t)(powf(b, 2.2f) * 4095.0f); }

// drive both LEDs together; LED 2 simply does nothing if it isn't wired
void redAll(uint16_t d)   { ledSet(LED_RED, d);   ledSet(LED2_RED, d); }
void greenAll(uint16_t d) { ledSet(LED_GREEN, d); ledSet(LED2_GREEN, d); }
void ledsOff()            { redAll(0); greenAll(0); }

void fadeAll(bool red, int ms) {
  for (int i = 0; i <= 100; i++)  { uint16_t d = gammaDuty(i / 100.0f); if (red) redAll(d); else greenAll(d); delay(ms / 100); }
  for (int i = 100; i >= 0; i--)  { uint16_t d = gammaDuty(i / 100.0f); if (red) redAll(d); else greenAll(d); delay(ms / 100); }
}

void ledTest() {
  Serial.println("\n== LED test (LED 1 on ch 0/1, LED 2 on ch 2/3) ==");
  if (!i2cPresent(PCA_ADDR)) { Serial.println("  PCA9685 not found"); return; }
  ledsOff();
  Serial.println("  red");            redAll(4095);   delay(800); ledsOff(); delay(200);
  Serial.println("  green");          greenAll(4095); delay(800); ledsOff(); delay(200);
  Serial.println("  both (orange)");  redAll(4095); greenAll(4095); delay(800); ledsOff(); delay(200);
  Serial.println("  fade red, then green");
  fadeAll(true, 1500); fadeAll(false, 1500);
  Serial.println("  done");
}

// read back PCA9685 registers: after boot expect MODE1=0x20 MODE2=0x04 PRESCALE=5
uint8_t pcaRead(uint8_t reg) {
  Wire.beginTransmission(PCA_ADDR); Wire.write(reg); Wire.endTransmission(false);
  Wire.requestFrom((int)PCA_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}
void pcaDump() {
  Serial.printf("MODE1=0x%02X MODE2=0x%02X PRESCALE=%u\n", pcaRead(0x00), pcaRead(0x01), pcaRead(0xFE));
  for (int ch = 0; ch < 4; ch++) {
    Serial.printf("  ch%d  ON=0x%02X%02X OFF=0x%02X%02X\n", ch,
                  pcaRead(0x06 + 4 * ch + 1), pcaRead(0x06 + 4 * ch),
                  pcaRead(0x06 + 4 * ch + 3), pcaRead(0x06 + 4 * ch + 2));
  }
}

// ---------- OLED ----------
void oledMsg(const char* l1, const char* l2 = "", const char* l3 = "") {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tr);
  oled.drawStr(0, 12, l1);
  oled.drawStr(0, 28, l2);
  oled.drawStr(0, 44, l3);
  oled.sendBuffer();
}

void oledTest() {
  Serial.println("\n== OLED test ==");
  if (!i2cPresent(OLED_ADDR)) { Serial.println("  OLED not found (try address 0x3D)"); return; }

  oledMsg("PowerEye node", "OLED test", "SSD1309 128x64");
  delay(1200);

  // all pixels on: look for dead pixels or dark rows
  oled.clearBuffer(); oled.setDrawColor(1); oled.drawBox(0, 0, 128, 64); oled.sendBuffer();
  delay(1000);

  // checkerboard
  oled.clearBuffer(); oled.setDrawColor(1);
  for (int y = 0; y < 64; y += 8)
    for (int x = 0; x < 128; x += 8)
      if (((x + y) / 8) % 2 == 0) oled.drawBox(x, y, 8, 8);
  oled.sendBuffer();
  delay(1000);

  // frame + corner markers: all four edges should be visible, not clipped
  oled.clearBuffer();
  oled.drawFrame(0, 0, 128, 64);
  oled.drawStr(4, 12, "TL");
  oled.drawStr(110, 12, "TR");
  oled.drawStr(4, 60, "BL");
  oled.drawStr(110, 60, "BR");
  oled.sendBuffer();
  delay(1200);

  // contrast sweep
  for (int c = 0; c <= 255; c += 5) { oled.setContrast(c); delay(15); }
  for (int c = 255; c >= 0; c -= 5) { oled.setContrast(c); delay(15); }
  oled.setContrast(128);

  // moving counter, so no static image sits on the panel (burn-in)
  for (int i = 0; i < 20; i++) {
    char buf[24]; snprintf(buf, sizeof(buf), "uptime %lus", millis() / 1000);
    oled.clearBuffer();
    oled.setFont(u8g2_font_10x20_tr);
    oled.drawStr(i * 3 % 40, 40, buf);
    oled.sendBuffer();
    delay(150);
  }
  oledMsg("OLED OK");
  Serial.println("  done: you should have seen text, white, checkerboard, frame, contrast, counter");
}

// ---------- serial commands ----------
void help() {
  Serial.println("\ncommands: s scan | o oled | l leds | r red | g green | y both | x off | d dump | a all | v saver | ? help");
}

void runAll() { i2cScan(); oledTest(); ledTest(); }

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);      // wait for USB CDC

  Wire.setPins(SDA_PIN, SCL_PIN);                          // set before oled.begin() starts Wire
  oled.begin();                                            // starts Wire on GPIO3/GPIO4
  Wire.setClock(I2C_HZ);
  oled.setBusClock(I2C_HZ);

  Serial.println("\nPowerEye I2C bring-up");
  Serial.printf("SDA=GPIO%d SCL=GPIO%d at %lu Hz\n", SDA_PIN, SCL_PIN, (unsigned long)I2C_HZ);

  if (i2cPresent(PCA_ADDR)) pcaInit(1000.0f);              // 1 kHz PWM for LEDs
  runAll();
  help();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 's': i2cScan(); break;
    case 'o': oledTest(); break;
    case 'l': ledTest(); break;
    case 'r': ledsOff(); redAll(4095); break;
    case 'g': ledsOff(); greenAll(4095); break;
    case 'y': redAll(4095); greenAll(4095); break;
    case 'x': ledsOff(); break;
    case 'd': pcaDump(); break;
    case 'a': runAll(); break;
    case 'v': Serial.println("screensaver: send any key to stop"); runScreensaver(oled, Serial); break;
    case '?': help(); break;
    default: break;
  }
}

