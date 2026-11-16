#include "saver.h"

namespace {

const uint32_t MODE_MS = 12000;
const int W = 128, H = 64;

// ---------- starfield ----------
struct Star { float x, y, z; };
Star stars[60];

void starReset(Star &s) {
  s.x = random(-W, W); s.y = random(-H, H); s.z = random(20, W);
}
void starsInit() { for (auto &s : stars) starReset(s); }
void starsFrame(U8G2 &d) {
  for (auto &s : stars) {
    s.z -= 1.6f;
    if (s.z < 1) starReset(s);
    int px = W / 2 + (int)(s.x * 64.0f / s.z);
    int py = H / 2 + (int)(s.y * 64.0f / s.z);
    if (px < 0 || px >= W || py < 0 || py >= H) { starReset(s); continue; }
    if (s.z < 40) d.drawBox(px, py, 2, 2); else d.drawPixel(px, py);
  }
}

// ---------- matrix rain ----------
const int COLS = 21;  // 6 px per column
int dropY[COLS], dropSpeed[COLS];
void rainInit() {
  for (int i = 0; i < COLS; i++) { dropY[i] = random(-H, 0); dropSpeed[i] = random(1, 4); }
}
void rainFrame(U8G2 &d) {
  d.setFont(u8g2_font_5x7_tf);
  for (int i = 0; i < COLS; i++) {
    for (int t = 0; t < 5; t++) {  // trail
      int y = dropY[i] - t * 8;
      if (y >= 0 && y < H) {
        char c = 33 + random(0, 90);
        if (t == 0) { d.setDrawColor(1); d.drawBox(i * 6, y - 7, 5, 8); d.setDrawColor(0); }
        d.drawGlyph(i * 6, y, c);
        d.setDrawColor(1);
      }
    }
    dropY[i] += dropSpeed[i];
    if (dropY[i] - 40 > H) { dropY[i] = random(-20, 0); dropSpeed[i] = random(1, 4); }
  }
}

// ---------- bouncing eye ----------
float ex = 30, ey = 20, vx = 1.3f, vy = 0.9f;
void eyeFrame(U8G2 &d, uint32_t t) {
  const int R = 14;
  ex += vx; ey += vy;
  if (ex < R + 2 || ex > W - R - 2) vx = -vx;
  if (ey < R + 2 || ey > H - R - 2) vy = -vy;
  int cx = (int)ex, cy = (int)ey;

  if ((t % 3000) < 150) {  // blink
    d.drawHLine(cx - R, cy, 2 * R);
    return;
  }
  d.drawEllipse(cx, cy, R, R - 3);
  int px = cx + (int)(vx * 4), py = cy + (int)(vy * 3);  // pupil looks where it's heading
  d.drawDisc(px, py, 5);
  d.setDrawColor(0); d.drawPixel(px - 2, py - 2); d.setDrawColor(1);
}

void startMode(uint8_t mode) {
  if (mode == 0) starsInit();
  if (mode == 1) rainInit();
}

}  // namespace

void runScreensaver(U8G2 &d, Stream &in) {
  while (in.available()) in.read();  // drop the command char that launched us
  randomSeed(micros());
  d.setContrast(80);                 // lower contrast = longer OLED life

  uint8_t mode = 0;
  uint32_t modeStart = millis();
  startMode(mode);

  while (!in.available()) {
    uint32_t now = millis();
    if (now - modeStart > MODE_MS) { mode = (mode + 1) % 3; modeStart = now; startMode(mode); }

    d.clearBuffer();
    d.setDrawColor(1);
    switch (mode) {
      case 0: starsFrame(d); break;
      case 1: rainFrame(d); break;
      case 2: eyeFrame(d, now); break;
    }
    d.sendBuffer();
    delay(40);
  }
  while (in.available()) in.read();

  d.setContrast(128);
  d.clearBuffer();
  d.setFont(u8g2_font_6x12_tr);
  d.drawStr(0, 12, "screensaver stopped");
  d.sendBuffer();
}

