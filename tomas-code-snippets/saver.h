#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// Playground OLED screensaver: starfield -> matrix rain -> bouncing eye, ~12 s each.
// Runs until any byte arrives on `in` (e.g. Serial), then restores contrast and returns.
void runScreensaver(U8G2 &d, Stream &in);

