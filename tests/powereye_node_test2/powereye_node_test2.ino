/*
    PowerEye live data analysis
    Board: Seeed XIAO ESP32-S3 with ADS131M02 (Arduino IDE)

    Reads the voltage and current channels from the ADC, samples them at
    SAMPLES_PER_SECOND, analyzes each window, and prints the measured results.
    V_SCALE and I_SCALE must be calibrated for the sensor/front-end conversions.
*/

#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

const bool USE_WARM_START = true;
#pragma GCC optimize ("O2")

// ===========================================================================
//  Sampling rate and window length used by the analysis.
// ===========================================================================
const int NOMINAL_FREQ_HZ          = 60;
const int SAMPLES_PER_SECOND       = 800;
const int AC_CYCLES_PER_DATAPOINT  = 10;
// Rounded to nearest whole sample count (133 for 10 cycles at 800 S/s).
constexpr int SAMPLES_PER_DATAPOINT =
    (AC_CYCLES_PER_DATAPOINT * SAMPLES_PER_SECOND * 2 + NOMINAL_FREQ_HZ) / (2 * NOMINAL_FREQ_HZ);

// ===========================================================================
//  Analysis limits. Adjust these for the monitored installation.
// ===========================================================================
const float ALLOWABLE_CURRENT_A      = 10.0f;
const float ALLOWABLE_VOLTAGE_V      = 120.0f;
const float VOLTAGE_TOLERANCE_PCT    = 5.0f;
const float FREQUENCY_TOLERANCE_HZ   = 0.5f;
const float VOLTAGE_THD_LIMIT_PCT    = 5.0f;
const float CURRENT_THD_LIMIT_PCT    = 5.0f;
const float RESIDUAL_LIMIT_PCT       = 3.0f;
const float MIN_AC_FREQ_HZ           = 1.0f;
const float MAX_RELIABLE_FREQ_HZ     = 300.0f;
const float MIN_SIGNAL_PEAK_V        = 0.5f;    // below this the voltage channel is treated as "no signal"
const int   NUMBER_OF_HARMONICS      = 15;      // upper limit; also capped by 300 Hz and Nyquist

// Frequency search tuning
const float FREQ_COLD_HALFWIDTH_HZ   = 0.25f;   // pass-1 spacing when starting from the zero-crossing estimate
const float FREQ_WARM_HALFWIDTH_HZ   = 0.08f;   // pass-1 spacing when starting from the previous frequency
const float FREQ_PASS2_FRACTION      = 0.2f;    // pass-2 spacing = this fraction of pass-1 spacing
const float WARM_START_MAX_DIFF_HZ   = 0.3f;    // use warm start only if previous freq is this close to zero-crossing estimate
const float FREQ_WIDE_SEARCH_PERCENT = 2.0f;    // fallback golden-section search width (+/- % of frequency)

// Calibrate these for the voltage/current sensor front ends.
const float V_OFFSET = 0.0f;
const float I_OFFSET = 0.0f;

// ===========================================================================
//  INTERNALS
// ===========================================================================
static_assert(NUMBER_OF_HARMONICS >= 1, "need at least 1 harmonic");
constexpr int   MAXP = 2 * NUMBER_OF_HARMONICS + 1;   // max unknowns in the least-squares fit
constexpr float TWO_PI_F = 6.28318530718f;

// Warning flags
enum : uint16_t {
  F_OVERVOLT    = 1 << 0,
  F_UNDERVOLT   = 1 << 1,
  F_OVERCURRENT = 1 << 2,
  F_FREQ        = 1 << 3,
  F_VTHD        = 1 << 4,
  F_ITHD        = 1 << 5,
  F_VRESID      = 1 << 6,
  F_IRESID      = 1 << 7,
  F_INVALID     = 1 << 8
};
const char* const FLAG_NAMES[] = {
  "Overvoltage", "Undervoltage", "Overcurrent", "Frequency out of tolerance",
  "Voltage THD too high", "Current THD too high",
  "Voltage waveform residual high", "Current waveform residual high",
  "Invalid window (no signal / too few cycles)"
};
const int NUM_FLAGS = sizeof(FLAG_NAMES) / sizeof(FLAG_NAMES[0]);

struct Result {
  bool     valid;
  float    vrms, irms, preal, sapp, qreact, pf;
  float    freq, freqZeroCross;
  float    vthd, ithd, vresid, iresid;
  int      harmonicsUsed;
  double   energyWh;     // energy for this datapoint only
  uint16_t flags;
};

// Frequency state is retained between consecutive live windows.
static float gLastFreq = 0.0f;

// Big work arrays are global (not on the stack) because the Arduino loop task has only ~8 KB of stack.
static float vRaw[2][SAMPLES_PER_DATAPOINT];
static float iRaw[2][SAMPLES_PER_DATAPOINT];
static float vw[SAMPLES_PER_DATAPOINT];       // cleaned voltage
static float iw[SAMPLES_PER_DATAPOINT];       // cleaned current
static float vac[SAMPLES_PER_DATAPOINT];      // voltage with mean removed (for zero crossings)
static float crossT[SAMPLES_PER_DATAPOINT];   // zero-crossing times, in samples
static float gMat[MAXP][MAXP];                // least-squares normal matrix
static float aug[MAXP][MAXP + 2];             // augmented matrix for the solver

// ADS131M02 wiring for the XIAO ESP32-S3 / ADC 15 Click.
const int PIN_SCK = D5;
const int PIN_MOSI = D6;
const int PIN_MISO = D7;
const int PIN_CS1 = D0;
const int PIN_CS2 = D4;
const int PIN_DRDY = D1;

const float ADC_VOLTS_PER_COUNT = 1.2f / 8388608.0f;
const float V_SCALE = 1.0f;
const float I_SCALE = 1.0f;

const uint16_t CMD_NULL = 0x0000;
const uint16_t CMD_RESET = 0x0011;

SPIClass adcSPI(FSPI);
SPISettings adcSettings(2000000, MSBFIRST, SPI_MODE1);
volatile uint32_t drdyCount = 0;
static int32_t latestVRaw = 0;
static int32_t latestIRaw = 0;
static uint32_t handledDrdy = 0;
static uint32_t lastSampleUs = 0;
static int samplesInWindow = 0;
static uint32_t datapointIndex = 0;
static bool hasAdcSample = false;
static uint32_t lastDrdyMillis = 0;
static QueueHandle_t freeWindows;
static QueueHandle_t readyWindows;
static int activeWindow = -1;

static void IRAM_ATTR onDrdy() {
  drdyCount++;
}

static int32_t signExt24(uint8_t b0, uint8_t b1, uint8_t b2) {
  int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
  if (value & 0x800000) value |= (int32_t)0xFF000000;
  return value;
}

static void adcFrame(uint16_t command, uint16_t& response, int32_t& ch0, int32_t& ch1) {
  uint8_t tx[12] = {0};
  uint8_t rx[12] = {0};
  tx[0] = command >> 8;
  tx[1] = command & 0xFF;
  adcSPI.beginTransaction(adcSettings);
  digitalWrite(PIN_CS1, LOW);
  adcSPI.transferBytes(tx, rx, sizeof(tx));
  digitalWrite(PIN_CS1, HIGH);
  adcSPI.endTransaction();
  response = ((uint16_t)rx[0] << 8) | rx[1];
  ch0 = signExt24(rx[3], rx[4], rx[5]);
  ch1 = signExt24(rx[6], rx[7], rx[8]);
}

static void readAvailableAdcFrames() {
  while (handledDrdy != drdyCount) {
    handledDrdy++;
    uint16_t response;
    adcFrame(CMD_NULL, response, latestVRaw, latestIRaw);
    if (!hasAdcSample) lastSampleUs = micros();
    hasAdcSample = true;
    lastDrdyMillis = millis();
  }
}

// ---------------------------------------------------------------------------
//  Least-squares helpers
//  Model: x(t) = c0 + sum_h [ s_h*sin(2*pi*h*f*t) + c_h*cos(2*pi*h*f*t) ]
//  Basis order: [1, sin1, cos1, sin2, cos2, ...]
//  sin/cos are advanced by a rotation recurrence (cheap), with step angles from
//  single-precision sinf/cosf (the ESP32-S3 FPU is single-precision only).
// ---------------------------------------------------------------------------

// Builds gMat (A^T A) and right-hand sides r1 (A^T x1), r2 (A^T x2, optional).
// Returns the number of unknowns p = 2*H + 1.
static int buildNormal(float f, int H, const float* x1, const float* x2, float* r1, float* r2) {
  const int p = 2 * H + 1;
  for (int a = 0; a < p; a++) {
    r1[a] = 0.0f;
    if (r2) r2[a] = 0.0f;
    for (int b = 0; b < p; b++) gMat[a][b] = 0.0f;
  }
  float sd[NUMBER_OF_HARMONICS + 1], cd[NUMBER_OF_HARMONICS + 1];
  float s[NUMBER_OF_HARMONICS + 1],  c[NUMBER_OF_HARMONICS + 1];
  const float w = TWO_PI_F * f / (float)SAMPLES_PER_SECOND;
  for (int h = 1; h <= H; h++) {
    sd[h] = sinf(h * w);
    cd[h] = cosf(h * w);
    s[h] = 0.0f;
    c[h] = 1.0f;
  }
  float b[MAXP];
  for (int n = 0; n < SAMPLES_PER_DATAPOINT; n++) {
    b[0] = 1.0f;
    for (int h = 1; h <= H; h++) { b[2 * h - 1] = s[h]; b[2 * h] = c[h]; }
    const float y1 = x1[n];
    const float y2 = x2 ? x2[n] : 0.0f;
    for (int a = 0; a < p; a++) {
      const float ba = b[a];
      r1[a] += ba * y1;
      if (r2) r2[a] += ba * y2;
      for (int k = a; k < p; k++) gMat[a][k] += ba * b[k];
    }
    for (int h = 1; h <= H; h++) {
      const float ns = s[h] * cd[h] + c[h] * sd[h];
      const float nc = c[h] * cd[h] - s[h] * sd[h];
      s[h] = ns;
      c[h] = nc;
    }
  }
  for (int a = 0; a < p; a++)
    for (int k = 0; k < a; k++) gMat[a][k] = gMat[k][a];
  return p;
}

// Solves gMat * x = r for one or two right-hand sides (Gaussian elimination
// with partial pivoting). r2 / x2 may be nullptr. Returns false if singular.
static bool solveNormal(int p, const float* r1, const float* r2, float* x1, float* x2) {
  const int W = p + 2;
  for (int a = 0; a < p; a++) {
    for (int b = 0; b < p; b++) aug[a][b] = gMat[a][b];
    aug[a][p]     = r1[a];
    aug[a][p + 1] = r2 ? r2[a] : 0.0f;
  }
  for (int col = 0; col < p; col++) {
    int piv = col;
    float best = fabsf(aug[col][col]);
    for (int row = col + 1; row < p; row++) {
      const float v = fabsf(aug[row][col]);
      if (v > best) { best = v; piv = row; }
    }
    if (best < 1e-9f) return false;
    if (piv != col) {
      for (int k = 0; k < W; k++) {
        const float tmp = aug[col][k];
        aug[col][k] = aug[piv][k];
        aug[piv][k] = tmp;
      }
    }
    for (int row = col + 1; row < p; row++) {
      const float fct = aug[row][col] / aug[col][col];
      if (fct == 0.0f) continue;
      for (int k = col; k < W; k++) aug[row][k] -= fct * aug[col][k];
    }
  }
  for (int rhs = 0; rhs < 2; rhs++) {
    float* x = (rhs == 0) ? x1 : x2;
    if (!x) continue;
    for (int row = p - 1; row >= 0; row--) {
      float sum = aug[row][p + rhs];
      for (int k = row + 1; k < p; k++) sum -= aug[row][k] * x[k];
      x[row] = sum / aug[row][row];
    }
  }
  return true;
}

// Sum of squared error of the best 3-parameter sine fit (offset + sin + cos)
// at a trial frequency. This is the function the frequency search minimises.
static float sineFitSSE(float f, const float* y) {
  float r[3], coef[3];
  buildNormal(f, 1, y, nullptr, r, nullptr);
  if (!solveNormal(3, r, nullptr, coef, nullptr)) return 1e30f;
  const float w = TWO_PI_F * f / (float)SAMPLES_PER_SECOND;
  const float sd = sinf(w), cd = cosf(w);
  float s = 0.0f, c = 1.0f, sse = 0.0f;
  for (int n = 0; n < SAMPLES_PER_DATAPOINT; n++) {
    // Second pass computes the error directly (avoids float cancellation).
    const float e = y[n] - (coef[0] + coef[1] * s + coef[2] * c);
    sse += e * e;
    const float ns = s * cd + c * sd;
    const float nc = c * cd - s * sd;
    s = ns;
    c = nc;
  }
  return sse;
}

// Wide golden-section search used when the fast refinement cannot bracket a minimum.
static float refineFrequencyWide(float f0, const float* y) {
  float a = f0 * (1.0f - FREQ_WIDE_SEARCH_PERCENT / 100.0f);
  float b = f0 * (1.0f + FREQ_WIDE_SEARCH_PERCENT / 100.0f);
  if (a < MIN_AC_FREQ_HZ) a = MIN_AC_FREQ_HZ;
  if (b > MAX_RELIABLE_FREQ_HZ) b = MAX_RELIABLE_FREQ_HZ;
  const float gr = 0.6180339887f;
  float c = b - gr * (b - a), d = a + gr * (b - a);
  float fc = sineFitSSE(c, y), fd = sineFitSSE(d, y);
  for (int it = 0; it < 22; it++) {
    if (fc < fd) { b = d; d = c; fd = fc; c = b - gr * (b - a); fc = sineFitSSE(c, y); }
    else         { a = c; c = d; fc = fd; d = a + gr * (b - a); fd = sineFitSSE(d, y); }
  }
  return 0.5f * (a + b);
}

// One parabolic step: evaluates the fit error at fc-h, fc, fc+h, fits a parabola
// through the three points and returns its lowest point. Near the true frequency
// the error curve is almost exactly a parabola, so this converges in a few steps.
// Returns false if the minimum is not bracketed by the three points.
static bool parabolaStep(float fc, float h, const float* y, float* fOut) {
  const float s0 = sineFitSSE(fc - h, y);
  const float s1 = sineFitSSE(fc, y);
  const float s2 = sineFitSSE(fc + h, y);
  const float denom = s0 - 2.0f * s1 + s2;
  if (!(denom > 0.0f)) return false;                 // curve is not bowl-shaped here
  if (s1 > s0 || s1 > s2) return false;              // minimum is outside the three points
  *fOut = fc + 0.5f * h * (s0 - s2) / denom;
  return true;
}

// Fast frequency refinement using two parabolic passes.
static float refineFrequency(float fStart, float halfWidth, float fZeroCross, const float* y) {
  float f1, f2;
  if (!parabolaStep(fStart, halfWidth, y, &f1)) {
    return refineFrequencyWide(fZeroCross, y);
  }
  if (parabolaStep(f1, halfWidth * FREQ_PASS2_FRACTION, y, &f2)) return f2;
  return f1;                                          // pass 1 alone is already close
}

// Waveform residual as % of the signal's AC RMS, after subtracting the fitted
// harmonic model. Catches noise, spikes and non-harmonic distortion.
static float residualPercent(float f, int H, const float* coef, const float* x) {
  const float w = TWO_PI_F * f / (float)SAMPLES_PER_SECOND;
  float sd[NUMBER_OF_HARMONICS + 1], cd[NUMBER_OF_HARMONICS + 1];
  float s[NUMBER_OF_HARMONICS + 1],  c[NUMBER_OF_HARMONICS + 1];
  for (int h = 1; h <= H; h++) { sd[h] = sinf(h * w); cd[h] = cosf(h * w); s[h] = 0.0f; c[h] = 1.0f; }
  float mean = 0.0f;
  for (int n = 0; n < SAMPLES_PER_DATAPOINT; n++) mean += x[n];
  mean /= SAMPLES_PER_DATAPOINT;
  float sseRes = 0.0f, sseAC = 0.0f;
  for (int n = 0; n < SAMPLES_PER_DATAPOINT; n++) {
    float fit = coef[0];
    for (int h = 1; h <= H; h++) fit += coef[2 * h - 1] * s[h] + coef[2 * h] * c[h];
    const float e = x[n] - fit;
    const float d = x[n] - mean;
    sseRes += e * e;
    sseAC  += d * d;
    for (int h = 1; h <= H; h++) {
      const float ns = s[h] * cd[h] + c[h] * sd[h];
      const float nc = c[h] * cd[h] - s[h] * sd[h];
      s[h] = ns; c[h] = nc;
    }
  }
  if (sseAC < 1e-12f) return 0.0f;
  return sqrtf(sseRes / sseAC) * 100.0f;
}

// ---------------------------------------------------------------------------
//  THE ANALYSIS
// ---------------------------------------------------------------------------
static void analyzeDatapoint(const float* vIn, const float* iIn, Result& r) {
  memset(&r, 0, sizeof(r));
  const int N = SAMPLES_PER_DATAPOINT;
  const float dtSec = (float)N / (float)SAMPLES_PER_SECOND;

  // --- 1. Apply calibrated channel offsets.
  for (int n = 0; n < N; n++) { vw[n] = vIn[n] - V_OFFSET; iw[n] = iIn[n] - I_OFFSET; }

  // --- 2. RMS, power, energy, power factor
  // Computed over the entire sampling window.
  float sv = 0.0f, si = 0.0f, sp = 0.0f, vmean = 0.0f;
  for (int n = 0; n < N; n++) {
    sv += vw[n] * vw[n];
    si += iw[n] * iw[n];
    sp += vw[n] * iw[n];
    vmean += vw[n];
  }
  vmean /= N;
  r.vrms  = sqrtf(sv / N);
  r.irms  = sqrtf(si / N);
  r.preal = sp / N;
  r.sapp  = r.vrms * r.irms;
  const float q2 = r.sapp * r.sapp - r.preal * r.preal;
  r.qreact = q2 > 0.0f ? sqrtf(q2) : 0.0f;
  r.pf = (r.sapp > 1e-9f) ? (r.preal / r.sapp) : NAN;      // guard: no-load divide by zero
  r.energyWh = (double)(r.preal * dtSec / 3600.0f);        // Wh (Python printed kWh)

  // --- 3. Simple limit checks
  const float vHi = ALLOWABLE_VOLTAGE_V * (1.0f + VOLTAGE_TOLERANCE_PCT / 100.0f);
  const float vLo = ALLOWABLE_VOLTAGE_V * (1.0f - VOLTAGE_TOLERANCE_PCT / 100.0f);
  if (r.vrms > vHi) r.flags |= F_OVERVOLT;
  else if (r.vrms < vLo) r.flags |= F_UNDERVOLT;
  if (r.irms > ALLOWABLE_CURRENT_A) r.flags |= F_OVERCURRENT;
  // --- 4. Coarse frequency from rising zero crossings.
  float peak = 0.0f;
  for (int n = 0; n < N; n++) {
    vac[n] = vw[n] - vmean;
    const float a = fabsf(vac[n]);
    if (a > peak) peak = a;
  }
  if (peak < MIN_SIGNAL_PEAK_V) { r.flags |= F_INVALID; return; }

  const float hyst = 0.05f * peak;      // ignore noise chatter near zero
  int nCross = 0;
  bool armed = false;
  for (int n = 0; n < N; n++) {
    if (vac[n] < -hyst) armed = true;
    else if (armed && vac[n] >= 0.0f && n > 0) {
      const float frac = -vac[n - 1] / (vac[n] - vac[n - 1]);   // linear interpolation
      crossT[nCross++] = (float)(n - 1) + frac;
      armed = false;
    }
  }
  if (nCross < 2) { r.flags |= F_INVALID; return; }
  const float span = crossT[nCross - 1] - crossT[0];
  if (span <= 0.0f) { r.flags |= F_INVALID; return; }
  r.freqZeroCross = (float)(nCross - 1) * (float)SAMPLES_PER_SECOND / span;
  if (r.freqZeroCross < MIN_AC_FREQ_HZ || r.freqZeroCross > MAX_RELIABLE_FREQ_HZ) { r.flags |= F_INVALID; return; }
  // --- 5. Fine frequency: parabolic refinement, warm-started when possible
  float fStart = r.freqZeroCross;
  float halfWidth = FREQ_COLD_HALFWIDTH_HZ;
  if (USE_WARM_START && gLastFreq > 0.0f && fabsf(gLastFreq - r.freqZeroCross) < WARM_START_MAX_DIFF_HZ) {
    fStart = gLastFreq;
    halfWidth = FREQ_WARM_HALFWIDTH_HZ;
  }
  r.freq = refineFrequency(fStart, halfWidth, r.freqZeroCross, vw);
  gLastFreq = r.freq;
  if (fabsf(r.freq - (float)NOMINAL_FREQ_HZ) > FREQUENCY_TOLERANCE_HZ) r.flags |= F_FREQ;
  // --- 6. Harmonic fit and THD
  int H = NUMBER_OF_HARMONICS;
  int hRel = (int)(MAX_RELIABLE_FREQ_HZ / r.freq);
  int hNyq = (int)((0.5f * SAMPLES_PER_SECOND) / r.freq);
  if ((float)hNyq * r.freq >= 0.5f * SAMPLES_PER_SECOND) hNyq--;   // stay strictly below Nyquist
  int hFit = (N - 1) / 2;                                          // need 2H+1 <= N unknowns
  if (hRel < H) H = hRel;
  if (hNyq < H) H = hNyq;
  if (hFit < H) H = hFit;
  if (H < 1) { r.flags |= F_INVALID; return; }
  r.harmonicsUsed = H;

  float rv[MAXP], ri[MAXP], cv[MAXP], ci[MAXP];
  const int p = buildNormal(r.freq, H, vw, iw, rv, ri);
  if (!solveNormal(p, rv, ri, cv, ci)) { r.flags |= F_INVALID; return; }

  // RMS of each harmonic, then THD = sqrt(sum of h>=2 squared) / fundamental
  float sumV2 = 0.0f, sumI2 = 0.0f, v1 = 0.0f, i1 = 0.0f;
  for (int h = 1; h <= H; h++) {
    const float vh = sqrtf(cv[2 * h - 1] * cv[2 * h - 1] + cv[2 * h] * cv[2 * h]) * 0.70710678f;
    const float ih = sqrtf(ci[2 * h - 1] * ci[2 * h - 1] + ci[2 * h] * ci[2 * h]) * 0.70710678f;
    if (h == 1) { v1 = vh; i1 = ih; }
    else        { sumV2 += vh * vh; sumI2 += ih * ih; }
  }
  r.vthd = (v1 > 1e-6f) ? sqrtf(sumV2) / v1 * 100.0f : NAN;   // NAN = fundamental too small (e.g. no load)
  r.ithd = (i1 > 1e-6f) ? sqrtf(sumI2) / i1 * 100.0f : NAN;
  r.vresid = residualPercent(r.freq, H, cv, vw);
  r.iresid = residualPercent(r.freq, H, ci, iw);

  if (!isnan(r.vthd) && r.vthd > VOLTAGE_THD_LIMIT_PCT) r.flags |= F_VTHD;
  if (!isnan(r.ithd) && r.ithd > CURRENT_THD_LIMIT_PCT) r.flags |= F_ITHD;
  if (r.vresid > RESIDUAL_LIMIT_PCT) r.flags |= F_VRESID;
  if (r.iresid > RESIDUAL_LIMIT_PCT) r.flags |= F_IRESID;
  r.valid = true;
}

struct WindowRef {
  uint8_t buffer;
  uint32_t index;
};

static uint16_t adcReadRegister(uint8_t address) {
  uint16_t response;
  int32_t ch0, ch1;
  const uint16_t command = 0xA000 | ((uint16_t)address << 7);
  adcFrame(command, response, ch0, ch1);
  adcFrame(CMD_NULL, response, ch0, ch1);
  return response;
}

static void printLiveResult(uint32_t index, const Result& result) {
  Serial.printf("\nWindow %lu\n", (unsigned long)index);
  Serial.printf("  Voltage RMS      : %.4f V\n", result.vrms);
  Serial.printf("  Current RMS      : %.4f A\n", result.irms);
  Serial.printf("  Real power       : %.4f W\n", result.preal);
  Serial.printf("  Apparent power   : %.4f VA\n", result.sapp);
  Serial.printf("  Reactive power   : %.4f var\n", result.qreact);
  Serial.printf("  Power factor     : %.4f\n", result.pf);
  Serial.printf("  Energy (window)  : %.6f Wh\n", result.energyWh);
  if (result.valid) {
    Serial.printf("  Frequency        : %.4f Hz\n", result.freq);
    Serial.printf("  Harmonics fitted : %d\n", result.harmonicsUsed);
    Serial.printf("  Voltage THD      : %.4f %%\n", result.vthd);
    Serial.printf("  Current THD      : %.4f %%\n", result.ithd);
    Serial.printf("  Voltage residual : %.4f %%\n", result.vresid);
    Serial.printf("  Current residual : %.4f %%\n", result.iresid);
  } else {
    Serial.println(F("  Frequency / harmonic analysis unavailable for this window."));
  }
  if (result.flags == 0) {
    Serial.println(F("  Alerts           : none"));
  } else {
    for (int bit = 0; bit < NUM_FLAGS; bit++) {
      if (result.flags & (1u << bit)) {
        Serial.printf("  ALERT            : %s\n", FLAG_NAMES[bit]);
      }
    }
  }
}

static void analysisTask(void*) {
  WindowRef window;
  for (;;) {
    if (xQueueReceive(readyWindows, &window, portMAX_DELAY) != pdTRUE) {
      Serial.println(F("Analysis task failed to receive a data window."));
      continue;
    }
    Result result;
    analyzeDatapoint(vRaw[window.buffer], iRaw[window.buffer], result);
    printLiveResult(window.index, result);
    if (xQueueSend(freeWindows, &window.buffer, portMAX_DELAY) != pdTRUE) {
      Serial.println(F("Analysis task could not release a data buffer."));
    }
  }
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 10000) delay(10);

  pinMode(PIN_CS1, OUTPUT);
  digitalWrite(PIN_CS1, HIGH);
  pinMode(PIN_CS2, OUTPUT);
  digitalWrite(PIN_CS2, HIGH);
  pinMode(PIN_DRDY, INPUT_PULLUP);
  adcSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);

  uint16_t response;
  int32_t ch0, ch1;
  adcFrame(CMD_RESET, response, ch0, ch1);
  delay(5);
  adcFrame(CMD_NULL, response, ch0, ch1);
  Serial.printf("ADS131M02 reset response: 0x%04X\n", response);
  Serial.printf("ADS131M02 ID register: 0x%04X\n", adcReadRegister(0x00));
  Serial.println(F("PowerEye live analysis started."));

  freeWindows = xQueueCreate(2, sizeof(uint8_t));
  readyWindows = xQueueCreate(2, sizeof(WindowRef));
  if (freeWindows == nullptr || readyWindows == nullptr) {
    Serial.println(F("Unable to create data-window queues."));
    while (true) delay(1000);
  }
  const uint8_t firstBuffer = 0;
  const uint8_t secondBuffer = 1;
  if (xQueueSend(freeWindows, &firstBuffer, 0) != pdTRUE ||
      xQueueSend(freeWindows, &secondBuffer, 0) != pdTRUE) {
    Serial.println(F("Unable to initialize the data-window buffers."));
    while (true) delay(1000);
  }

  if (xTaskCreatePinnedToCore(analysisTask, "PowerEyeAnalysis", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println(F("Unable to start the analysis task."));
    while (true) delay(1000);
  }

  attachInterrupt(digitalPinToInterrupt(PIN_DRDY), onDrdy, FALLING);
  lastDrdyMillis = millis();
}

void loop() {
  readAvailableAdcFrames();
  const uint32_t nowUs = micros();
  const uint32_t sampleIntervalUs = 1000000UL / SAMPLES_PER_SECOND;

  if (hasAdcSample && (uint32_t)(nowUs - lastSampleUs) >= sampleIntervalUs) {
    lastSampleUs = nowUs;
    if (activeWindow < 0) {
      uint8_t availableBuffer;
      if (xQueueReceive(freeWindows, &availableBuffer, 0) == pdTRUE) {
        activeWindow = availableBuffer;
        samplesInWindow = 0;
      }
    }

    if (activeWindow >= 0) {
      const int sample = samplesInWindow++;
      vRaw[activeWindow][sample] = latestVRaw * ADC_VOLTS_PER_COUNT * V_SCALE;
      iRaw[activeWindow][sample] = latestIRaw * ADC_VOLTS_PER_COUNT * I_SCALE;

      if (samplesInWindow == SAMPLES_PER_DATAPOINT) {
        WindowRef completed = {(uint8_t)activeWindow, datapointIndex++};
        if (xQueueSend(readyWindows, &completed, 0) != pdTRUE) {
          Serial.println(F("Analysis queue is full; dropping a completed data window."));
          xQueueSend(freeWindows, &completed.buffer, 0);
        }
        activeWindow = -1;
        samplesInWindow = 0;
      }
    }
  }

  if ((uint32_t)(millis() - lastDrdyMillis) >= 1000) {
    Serial.println(F("No ADC data-ready samples received; check DRDY wiring and ADC power."));
    lastDrdyMillis = millis();
    hasAdcSample = false;
    if (activeWindow >= 0) {
      const uint8_t abandonedBuffer = (uint8_t)activeWindow;
      xQueueSend(freeWindows, &abandonedBuffer, 0);
      activeWindow = -1;
      samplesInWindow = 0;
    }
  }
}
