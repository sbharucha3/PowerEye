/*
  ============================================================================
  PowerEye - Node analysis + timing benchmark  (v2, optimized)
  Board: Seeed XIAO ESP32-S3 (Arduino IDE)
  ============================================================================

  
  LIBRARIES NEEDED
    None. Uses only Arduino.h and math.h from the ESP32 board package.

  HOW TO RUN
    1. Arduino IDE > Tools: Board = XIAO_ESP32S3, USB CDC On Boot = Enabled, Port = yours.
    2. Settings are at the top (RUN_ALL_SIZES, NUM_DATAPOINTS, switches below).
    3. Close any open Serial Monitor, then click Upload.
    4. Right after upload, open Serial Monitor at 115200 baud (sketch waits up to 10 s).
    5. Type any character + Enter to run again. If blank, press RESET.


  HOW TO REPLACE THE FAKE DATA LATER
    analyzeDatapoint() takes two float arrays (voltage, current) of
    SAMPLES_PER_DATAPOINT samples. Replace generateDatapoint() with your ADC read.
  ============================================================================
*/

#include <Arduino.h>
#include <math.h>
#include <string.h>

// ===========================================================================
//  >>> BENCHMARK SETTINGS - CHANGE THESE <<<
// ===========================================================================
const bool RUN_ALL_SIZES  = true;   // true = run 1, 10, 100, 1000 in a row; false = run only NUM_DATAPOINTS
const int  NUM_DATAPOINTS = 100;    // used only when RUN_ALL_SIZES is false: 1, 10, 100, or 1000
const bool PRINT_DETAILS  = true;   // print the full stats block for the very first datapoint
const bool STAGE_TIMING   = true;   // measure time per analysis stage (tiny overhead)

// ===========================================================================
//  SPEED SWITCHES (A/B these)
// ===========================================================================
#define USE_O2_PRAGMA 1             // 1 = ask the compiler for -O2 speed optimization in this file
const bool USE_WARM_START = true;  // start the search from the previous datapoint's frequency. OFF by default:
                                    // in testing it gave no speed-up and falls back to the slow search when
                                    // the frequency moves fast. Flip to true to A/B it on real data.

#if USE_O2_PRAGMA
#pragma GCC optimize ("O2")
#endif

// ===========================================================================
//  SAMPLING PLAN (same as the Python prototype)
// ===========================================================================
const int NOMINAL_FREQ_HZ          = 60;
const int SAMPLES_PER_SECOND       = 800;
const int AC_CYCLES_PER_DATAPOINT  = 10;
// Rounded to nearest whole sample count (133 for 10 cycles at 800 S/s).
constexpr int SAMPLES_PER_DATAPOINT =
    (AC_CYCLES_PER_DATAPOINT * SAMPLES_PER_SECOND * 2 + NOMINAL_FREQ_HZ) / (2 * NOMINAL_FREQ_HZ);

// ===========================================================================
//  LIMITS AND ANALYSIS SETTINGS (ASSUMPTIONS - match your real device later)
// ===========================================================================
const float ALLOWABLE_CURRENT_A      = 10.0f;
const float ALLOWABLE_VOLTAGE_V      = 20.0f;   // bench value from Python; real node would be 120 V
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

// Calibration offsets (stub for the "clean up" stage; subtracted before analysis)
const float V_OFFSET = 0.0f;
const float I_OFFSET = 0.0f;

// ===========================================================================
//  SYNTHETIC DATA SETTINGS (fake signal so the code runs without hardware)
// ===========================================================================
const float SIM_VOLTAGE_PEAK_V   = 28.0f;
const float SIM_CURRENT_PEAK_A   = 10.0f;
const float SIM_FREQUENCY_HZ     = 60.2f;   // centre frequency
const float SIM_FREQ_WANDER_HZ   = 0.2f;    // slow +/- wander around the centre (0 = perfectly steady)
const int   SIM_FREQ_WANDER_PERIOD = 50;    // datapoints per full wander cycle
const float SIM_CURRENT_LAG_DEG  = 30.0f;   // current lags voltage
const float SIM_V_HARMONIC3      = 0.02f;   // 3rd harmonic, fraction of fundamental (2%)
const float SIM_I_HARMONIC3      = 0.15f;   // 3rd harmonic, fraction of fundamental (15%)
const float SIM_NOISE_V          = 0.10f;   // uniform noise amplitude, volts
const float SIM_NOISE_I          = 0.05f;   // uniform noise amplitude, amps
const uint32_t RNG_SEED          = 12345;

// Accuracy targets used for the PASS/FAIL lines
const float TARGET_VRMS_PCT   = 0.5f;       // project requirement
const float TARGET_IRMS_PCT   = 3.0f;       // project requirement
const float TARGET_FREQ_HZ    = 0.01f;      // our own goal
const float TARGET_THD_PP     = 0.5f;       // percentage points, our own goal

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
const char* const FLAG_SHORT[] = { "OV", "UV", "OC", "FREQ", "VTHD", "ITHD", "VRES", "IRES", "INVALID" };
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

// Stages of the analysis, for the timing breakdown
enum { ST_BASIC = 0, ST_ZEROX, ST_FREQ, ST_HARM, ST_RESID, NUM_STAGES };
const char* const STAGE_NAMES[NUM_STAGES] = {
  "Basic (RMS/power/limits)", "Zero-crossings", "Frequency fit", "Harmonic fit + THD", "Residuals"
};
static uint32_t stageUs[NUM_STAGES];
static uint32_t stageT0;
#define STAGE_BEGIN()  do { if (STAGE_TIMING) stageT0 = micros(); } while (0)
#define STAGE_END(k)   do { if (STAGE_TIMING) { uint32_t _t = micros(); stageUs[k] += _t - stageT0; stageT0 = _t; } } while (0)

// Metrics tracked in the batch report
enum { M_VRMS = 0, M_IRMS, M_P, M_S, M_PF, M_FREQ, M_VTHD, M_ITHD, M_VRES, M_IRES, NUM_METRICS };
const char* const METRIC_NAMES[NUM_METRICS] = {
  "Vrms (V)", "Irms (A)", "Real power (W)", "Apparent (VA)", "Power factor",
  "Frequency (Hz)", "Voltage THD (%)", "Current THD (%)", "V residual (%)", "I residual (%)"
};

struct BenchStats {
  int      count;
  double   totalMs;
  double   avgUs;
  uint32_t minUs, maxUs;
  double   stageAvgUs[NUM_STAGES];
};

// State carried between datapoints (for the warm start) and diagnostics
static float    gLastFreq = 0.0f;       // 0 = none yet
static uint32_t gFallbackCount = 0;     // times the parabolic search failed and the wide search ran
static void resetAnalysisState() { gLastFreq = 0.0f; gFallbackCount = 0; }

// Big work arrays are global (not on the stack) because the Arduino loop task has only ~8 KB of stack.
static float vRaw[SAMPLES_PER_DATAPOINT];
static float iRaw[SAMPLES_PER_DATAPOINT];
static float vw[SAMPLES_PER_DATAPOINT];       // cleaned voltage
static float iw[SAMPLES_PER_DATAPOINT];       // cleaned current
static float vac[SAMPLES_PER_DATAPOINT];      // voltage with mean removed (for zero crossings)
static float crossT[SAMPLES_PER_DATAPOINT];   // zero-crossing times, in samples
static float gMat[MAXP][MAXP];                // least-squares normal matrix
static float aug[MAXP][MAXP + 2];             // augmented matrix for the solver

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

// FALLBACK: wide golden-section search (24 sine fits). Slow but very robust.
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

// Fast frequency refinement: 2 parabolic passes = 6 sine fits (vs 24 for the wide search).
static float refineFrequency(float fStart, float halfWidth, float fZeroCross, const float* y) {
  float f1, f2;
  if (!parabolaStep(fStart, halfWidth, y, &f1)) {
    gFallbackCount++;
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
//  THE ANALYSIS (this is what gets timed)
// ---------------------------------------------------------------------------
static void analyzeDatapoint(const float* vIn, const float* iIn, Result& r) {
  memset(&r, 0, sizeof(r));
  const int N = SAMPLES_PER_DATAPOINT;
  const float dtSec = (float)N / (float)SAMPLES_PER_SECOND;

  STAGE_BEGIN();

  // --- 1. Clean up (stub: offset removal only; low-pass and gap checks go here later)
  for (int n = 0; n < N; n++) { vw[n] = vIn[n] - V_OFFSET; iw[n] = iIn[n] - I_OFFSET; }

  // --- 2. RMS, power, energy, power factor
  // NOTE: computed over the whole window. If the grid frequency is far from 60 Hz the window is not a
  // whole number of cycles, which adds error (about 0.3% at 59.5 Hz, 0.7% at 59.3 Hz in testing).
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
  STAGE_END(ST_BASIC);

  // --- 4. Coarse frequency from rising zero crossings (replaces the Python FFT peak)
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
  STAGE_END(ST_ZEROX);

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
  STAGE_END(ST_FREQ);

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
  STAGE_END(ST_HARM);

  r.vresid = residualPercent(r.freq, H, cv, vw);
  r.iresid = residualPercent(r.freq, H, ci, iw);

  if (!isnan(r.vthd) && r.vthd > VOLTAGE_THD_LIMIT_PCT) r.flags |= F_VTHD;
  if (!isnan(r.ithd) && r.ithd > CURRENT_THD_LIMIT_PCT) r.flags |= F_ITHD;
  if (r.vresid > RESIDUAL_LIMIT_PCT) r.flags |= F_VRESID;
  if (r.iresid > RESIDUAL_LIMIT_PCT) r.flags |= F_IRESID;
  STAGE_END(ST_RESID);

  r.valid = true;
}

// ---------------------------------------------------------------------------
//  SYNTHETIC DATA SOURCE (replace with real ADC reads later; not timed)
// ---------------------------------------------------------------------------
static uint32_t rngState = RNG_SEED;
static float randSym() {   // uniform in [-1, 1)
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return ((rngState & 0xFFFFFF) / 8388608.0f) - 1.0f;
}

// Fills v and i for datapoint number `index`. Also returns the true frequency used,
// so the report can compare the measured frequency against ground truth.
static void generateDatapoint(int index, float* v, float* i, float* trueFreq) {
  const float fd = SIM_FREQUENCY_HZ + SIM_FREQ_WANDER_HZ *
                   sinf(TWO_PI_F * (float)(index % SIM_FREQ_WANDER_PERIOD) / (float)SIM_FREQ_WANDER_PERIOD);
  *trueFreq = fd;
  const float x = (float)index * 0.618034f;
  const float phi0 = TWO_PI_F * (x - floorf(x));       // varies the starting phase per datapoint
  const float w = TWO_PI_F * fd / (float)SAMPLES_PER_SECOND;
  const float lag = SIM_CURRENT_LAG_DEG * (float)M_PI / 180.0f;
  for (int n = 0; n < SAMPLES_PER_DATAPOINT; n++) {
    const float ph = w * (float)n + phi0;
    v[n] = SIM_VOLTAGE_PEAK_V * (sinf(ph) + SIM_V_HARMONIC3 * sinf(3.0f * ph)) + SIM_NOISE_V * randSym();
    i[n] = SIM_CURRENT_PEAK_A * (sinf(ph - lag) + SIM_I_HARMONIC3 * sinf(3.0f * ph)) + SIM_NOISE_I * randSym();
  }
}

// What the synthetic signal should give (frequency varies per datapoint, so NAN here).
static void computeExpected(float* e) {
  const float lag = SIM_CURRENT_LAG_DEG * (float)M_PI / 180.0f;
  const float vr = SIM_VOLTAGE_PEAK_V / 1.41421356f * sqrtf(1.0f + SIM_V_HARMONIC3 * SIM_V_HARMONIC3);
  const float ir = SIM_CURRENT_PEAK_A / 1.41421356f * sqrtf(1.0f + SIM_I_HARMONIC3 * SIM_I_HARMONIC3);
  const float p  = 0.5f * SIM_VOLTAGE_PEAK_V * SIM_CURRENT_PEAK_A * cosf(lag)
                 + 0.5f * (SIM_VOLTAGE_PEAK_V * SIM_V_HARMONIC3) * (SIM_CURRENT_PEAK_A * SIM_I_HARMONIC3);
  for (int k = 0; k < NUM_METRICS; k++) e[k] = NAN;
  e[M_VRMS] = vr;
  e[M_IRMS] = ir;
  e[M_P]    = p;
  e[M_S]    = vr * ir;
  e[M_PF]   = p / (vr * ir);
  e[M_VTHD] = SIM_V_HARMONIC3 * 100.0f;
  e[M_ITHD] = SIM_I_HARMONIC3 * 100.0f;
}

// ---------------------------------------------------------------------------
//  PRINTING
// ---------------------------------------------------------------------------
static void printResult(const Result& r) {
  Serial.println(F("  --- First datapoint, full stats ---"));
  if (!r.valid) Serial.println(F("  (window invalid: frequency/THD not computed)"));
  Serial.printf("  Voltage RMS      : %.4f V\n", r.vrms);
  Serial.printf("  Current RMS      : %.4f A\n", r.irms);
  Serial.printf("  Real power       : %.4f W\n", r.preal);
  Serial.printf("  Apparent power   : %.4f VA\n", r.sapp);
  Serial.printf("  Reactive power   : %.4f var\n", r.qreact);
  Serial.printf("  Power factor     : %.4f\n", r.pf);
  Serial.printf("  Energy (this pt) : %.6f Wh\n", r.energyWh);
  if (r.valid) {
    Serial.printf("  Frequency        : %.4f Hz  (zero-cross estimate %.4f Hz)\n", r.freq, r.freqZeroCross);
    Serial.printf("  Harmonics fitted : %d\n", r.harmonicsUsed);
    Serial.printf("  Voltage THD      : %.4f %%\n", r.vthd);
    Serial.printf("  Current THD      : %.4f %%\n", r.ithd);
    Serial.printf("  Voltage residual : %.4f %%\n", r.vresid);
    Serial.printf("  Current residual : %.4f %%\n", r.iresid);
  }
  if (r.flags == 0) {
    Serial.println(F("  Alerts           : none"));
  } else {
    for (int b = 0; b < NUM_FLAGS; b++)
      if (r.flags & (1u << b)) Serial.printf("  ALERT            : %s\n", FLAG_NAMES[b]);
  }
}

static const char* flagString(uint16_t flags) {
  static char buf[64];
  buf[0] = '\0';
  if (flags == 0) { strcpy(buf, "none"); return buf; }
  for (int b = 0; b < NUM_FLAGS; b++) {
    if (flags & (1u << b)) {
      if (buf[0]) strcat(buf, ",");
      strcat(buf, FLAG_SHORT[b]);
    }
  }
  return buf;
}

static void printCompactHeader() {
  Serial.println(F("   idx |  Vrms (V) | Irms (A) |   P (W)  |   PF   |  f (Hz)  | THDv % | THDi % | alerts"));
  Serial.println(F("  -----+-----------+----------+----------+--------+----------+--------+--------+-------"));
}

static void printCompactRow(int idx, const Result& r) {
  Serial.printf("  %4d | %9.4f | %8.4f | %8.3f | %6.4f | %8.4f | %6.3f | %6.3f | %s\n",
                idx, r.vrms, r.irms, r.preal, r.pf, r.freq, r.vthd, r.ithd, flagString(r.flags));
}

static void printStatRow(const char* name, double mean, float mn, float mx, float expected) {
  if (isnan(expected)) Serial.printf("  %-16s | %11.4f | %11.4f | %11.4f | %11s\n", name, mean, mn, mx, "-");
  else                 Serial.printf("  %-16s | %11.4f | %11.4f | %11.4f | %11.4f\n", name, mean, mn, mx, expected);
}

static void printCheckRow(const char* name, float worst, const char* unit, float target) {
  Serial.printf("  %-18s : worst %8.4f %-4s (target %.3f %s)  %s\n",
                name, worst, unit, target, unit, (worst <= target) ? "PASS" : "FAIL");
}

// ---------------------------------------------------------------------------
//  BENCHMARK RUNNER (times only analyzeDatapoint; everything else is outside the timer)
// ---------------------------------------------------------------------------
static const int KEEP = 5;   // how many first/last datapoints to show as one-line rows

static BenchStats runBatch(int count, bool showFirstDetails) {
  rngState = RNG_SEED;          // same fake data every run, so runs are comparable
  resetAnalysisState();         // each batch starts "cold" (no previous frequency)
  memset(stageUs, 0, sizeof(stageUs));

  BenchStats st;
  memset(&st, 0, sizeof(st));
  st.count = count; st.minUs = 0xFFFFFFFFu;
  uint64_t totalUs = 0;

  static Result res, head[KEEP], tail[KEEP];
  double sum[NUM_METRICS];
  float  mn[NUM_METRICS], mx[NUM_METRICS];
  for (int k = 0; k < NUM_METRICS; k++) { sum[k] = 0.0; mn[k] = INFINITY; mx[k] = -INFINITY; }
  float expected[NUM_METRICS];
  computeExpected(expected);

  int validCount = 0, thdCount = 0;
  uint32_t flagCount[NUM_FLAGS];
  for (int b = 0; b < NUM_FLAGS; b++) flagCount[b] = 0;
  double trueFreqSum = 0.0;
  float worstV = 0, worstI = 0, worstP = 0, worstF = 0, worstTv = 0, worstTi = 0;

  for (int d = 0; d < count; d++) {
    float trueF;
    generateDatapoint(d, vRaw, iRaw, &trueF);   // NOT timed

    const uint32_t t0 = micros();
    analyzeDatapoint(vRaw, iRaw, res);          // timed
    const uint32_t dt = micros() - t0;

    totalUs += dt;
    if (dt < st.minUs) st.minUs = dt;
    if (dt > st.maxUs) st.maxUs = dt;

    // ---- bookkeeping below is outside the timed section ----
    if (d < KEEP) head[d] = res;
    tail[d % KEEP] = res;
    trueFreqSum += trueF;
    for (int b = 0; b < NUM_FLAGS; b++) if (res.flags & (1u << b)) flagCount[b]++;

    if (res.valid) {
      validCount++;
      const float vals[NUM_METRICS] = { res.vrms, res.irms, res.preal, res.sapp, res.pf,
                                        res.freq, res.vthd, res.ithd, res.vresid, res.iresid };
      for (int k = 0; k < NUM_METRICS; k++) {
        if (isnan(vals[k])) continue;
        sum[k] += vals[k];
        if (vals[k] < mn[k]) mn[k] = vals[k];
        if (vals[k] > mx[k]) mx[k] = vals[k];
      }
      if (!isnan(res.vthd)) thdCount++;
      float e;
      e = fabsf(res.vrms - expected[M_VRMS]) / expected[M_VRMS] * 100.0f; if (e > worstV) worstV = e;
      e = fabsf(res.irms - expected[M_IRMS]) / expected[M_IRMS] * 100.0f; if (e > worstI) worstI = e;
      e = fabsf(res.preal - expected[M_P]) / expected[M_P] * 100.0f;       if (e > worstP) worstP = e;
      e = fabsf(res.freq - trueF);                                         if (e > worstF) worstF = e;
      if (!isnan(res.vthd)) { e = fabsf(res.vthd - expected[M_VTHD]); if (e > worstTv) worstTv = e; }
      if (!isnan(res.ithd)) { e = fabsf(res.ithd - expected[M_ITHD]); if (e > worstTi) worstTi = e; }
    }
    if (showFirstDetails && d == 0 && PRINT_DETAILS) printResult(res);
    yield();
  }

  st.totalMs = totalUs / 1000.0;
  st.avgUs   = (double)totalUs / (double)count;
  for (int s = 0; s < NUM_STAGES; s++) st.stageAvgUs[s] = (double)stageUs[s] / (double)count;
  expected[M_FREQ] = (float)(trueFreqSum / count);

  // ---------------- report ----------------
  Serial.println();
  Serial.printf("  ===== Batch of %d datapoint(s) =====\n", count);

  // 1. one-line rows for first and last datapoints
  Serial.println(F("  Individual datapoints (first and last):"));
  printCompactHeader();
  const int nHead = (count < KEEP) ? count : KEEP;
  for (int d = 0; d < nHead; d++) printCompactRow(d, head[d]);
  int tailStart = count - KEEP;
  if (tailStart < KEEP) tailStart = KEEP;
  if (tailStart > KEEP) Serial.println(F("   ... |    ...    |   ...    |   ...    |  ...   |   ...    |  ...   |  ...   | ..."));
  for (int d = tailStart; d < count; d++) printCompactRow(d, tail[d % KEEP]);

  // 2. statistics across the whole batch
  Serial.println();
  Serial.printf("  Statistics across all %d datapoint(s) (%d valid):\n", count, validCount);
  Serial.println(F("  Metric           |        mean |         min |         max |    expected"));
  Serial.println(F("  -----------------+-------------+-------------+-------------+------------"));
  for (int k = 0; k < NUM_METRICS; k++) {
    int n = (k == M_VTHD || k == M_ITHD) ? thdCount : validCount;
    if (n > 0) printStatRow(METRIC_NAMES[k], sum[k] / n, mn[k], mx[k], expected[k]);
  }
  Serial.println(F("  (expected frequency = average of the true wandering frequency; residual has no expected value)"));

  // 3. accuracy vs ground truth
  Serial.println();
  Serial.println(F("  Worst-case error vs. known synthetic truth:"));
  printCheckRow("Vrms",        worstV,  "%",  TARGET_VRMS_PCT);
  printCheckRow("Irms",        worstI,  "%",  TARGET_IRMS_PCT);
  printCheckRow("Frequency",   worstF,  "Hz", TARGET_FREQ_HZ);
  printCheckRow("Voltage THD", worstTv, "pp", TARGET_THD_PP);
  printCheckRow("Current THD", worstTi, "pp", TARGET_THD_PP);
  Serial.printf("  %-18s : worst %8.4f %%\n", "Real power", worstP);

  // 4. alerts across the batch
  Serial.println();
  Serial.println(F("  Alerts raised (datapoints affected):"));
  bool any = false;
  for (int b = 0; b < NUM_FLAGS; b++)
    if (flagCount[b]) { Serial.printf("    %-45s %lu of %d\n", FLAG_NAMES[b], (unsigned long)flagCount[b], count); any = true; }
  if (!any) Serial.println(F("    none"));
  Serial.println(F("  (Current THD alerts are EXPECTED here: the fake current has 15% THD vs a 5% limit.)"));
  Serial.printf("  Invalid datapoints: %d | Frequency search fallbacks to wide search: %lu\n",
                count - validCount, (unsigned long)gFallbackCount);

  // 5. timing
  const double dpUs = 1e6 * (double)SAMPLES_PER_DATAPOINT / (double)SAMPLES_PER_SECOND;
  Serial.println();
  Serial.println(F("  Timing (analysis only):"));
  Serial.printf("    Total              : %.3f ms\n", st.totalMs);
  Serial.printf("    Average / datapoint: %.1f us   (fastest %lu us, slowest %lu us)\n",
                st.avgUs, (unsigned long)st.minUs, (unsigned long)st.maxUs);
  Serial.printf("    Real-time CPU load : %.2f %%  (signal time per datapoint = %.2f ms)\n",
                100.0 * st.avgUs / dpUs, dpUs / 1000.0);
  if (STAGE_TIMING) {
    double stageSum = 0.0;
    for (int s = 0; s < NUM_STAGES; s++) stageSum += st.stageAvgUs[s];
    Serial.println(F("    Per-stage average per datapoint:"));
    for (int s = 0; s < NUM_STAGES; s++)
      Serial.printf("      %-26s %9.1f us  %5.1f %%\n", STAGE_NAMES[s], st.stageAvgUs[s],
                    stageSum > 0 ? 100.0 * st.stageAvgUs[s] / stageSum : 0.0);
  } else {
    Serial.println(F("    (stage timing off)"));
  }
  return st;
}

static void runRequested() {
  Serial.println();
  Serial.println(F("============================================================"));
  Serial.println(F(" PowerEye analysis benchmark (v2 optimized)"));
  Serial.println(F("============================================================"));
  Serial.printf(" CPU: %d MHz | %d samples/datapoint | %d cycles/datapoint\n",
                (int)getCpuFrequencyMhz(), SAMPLES_PER_DATAPOINT, AC_CYCLES_PER_DATAPOINT);
  Serial.printf(" Switches: warm start %s | O2 pragma %s | stage timing %s\n",
                USE_WARM_START ? "ON" : "off", USE_O2_PRAGMA ? "ON" : "off", STAGE_TIMING ? "ON" : "off");

  if (RUN_ALL_SIZES) {
    const int sizes[4] = {1, 10, 100, 1000};
    BenchStats results[4];
    for (int k = 0; k < 4; k++) results[k] = runBatch(sizes[k], k == 0);

    Serial.println();
    Serial.println(F("============================================================"));
    Serial.println(F(" SUMMARY"));
    Serial.println(F("============================================================"));
    Serial.println(F(" Datapoints | Total (ms) | Avg/point (us) | Min (us) | Max (us)"));
    Serial.println(F(" -----------+------------+----------------+----------+---------"));
    for (int k = 0; k < 4; k++)
      Serial.printf(" %10d | %10.3f | %14.1f | %8lu | %8lu\n", results[k].count, results[k].totalMs,
                    results[k].avgUs, (unsigned long)results[k].minUs, (unsigned long)results[k].maxUs);
    if (STAGE_TIMING) {
      Serial.println();
      Serial.println(F(" Average microseconds per datapoint, by stage:"));
      Serial.println(F(" Datapoints |   Basic |  ZeroX | FreqFit | Harmonic | Residual"));
      Serial.println(F(" -----------+---------+--------+---------+----------+---------"));
      for (int k = 0; k < 4; k++)
        Serial.printf(" %10d | %7.1f | %6.1f | %7.1f | %8.1f | %8.1f\n", results[k].count,
                      results[k].stageAvgUs[ST_BASIC], results[k].stageAvgUs[ST_ZEROX],
                      results[k].stageAvgUs[ST_FREQ], results[k].stageAvgUs[ST_HARM], results[k].stageAvgUs[ST_RESID]);
    }
  } else {
    runBatch(NUM_DATAPOINTS, true);
  }
  Serial.println(F("============================================================"));
  Serial.println(F(" Done. Type any character and press Enter to run again."));
}

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 10000) delay(10);   // wait for Serial Monitor (USB CDC)
  delay(500);
  runRequested();
}

void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    runRequested();
  }
  delay(10);
}
