/*
  ============================================================================
  PowerEye - Node analysis + timing benchmark
  Board: Seeed XIAO ESP32-S3 (Arduino IDE)
  ============================================================================

  WHAT THIS DOES
    Generates synthetic voltage/current datapoints (fake ADC data), runs the
    same analysis as the Python prototype on each one, and reports how long
    the analysis took for a batch of 1, 10, 100 or 1000 datapoints.
    Only the ANALYSIS is timed. Data generation and printing are not.

  LIBRARIES NEEDED
    None. Uses only Arduino.h and math.h, which come with the ESP32 board
    package you already installed. No FFT library is used: the analysis is a
    zero-crossing frequency estimate, a sine-fit refinement, and a
    least-squares harmonic fit (the same method as the Python code).

  HOW TO RUN
    1. Arduino IDE > Tools, set:
         Board:              XIAO_ESP32S3
         USB CDC On Boot:    Enabled        <-- required to see Serial output
         Port:               your COM port
    2. Change NUM_DATAPOINTS below (1, 10, 100 or 1000).
    3. Close any open Serial Monitor, then click Upload.
    4. As soon as upload finishes, open Tools > Serial Monitor, set 115200
       baud. The sketch waits up to 10 s for the monitor to open.
    5. Results print once. Type any character and press Enter to run again.
       If nothing appears, press the RESET button on the board.

  HOW TO REPLACE THE FAKE DATA LATER
    analyzeDatapoint() takes two float arrays (voltage, current) of
    SAMPLES_PER_DATAPOINT samples. Replace generateDatapoint() with your ADC
    read loop that fills those arrays; nothing else needs to change.
  ============================================================================
*/

#include <Arduino.h>
#include <math.h>
#include <string.h>

// ===========================================================================
//  >>> BENCHMARK SETTINGS - CHANGE THESE <<<
// ===========================================================================
const int  NUM_DATAPOINTS = 10;    // <<< 1, 10, 100, or 1000
const bool RUN_ALL_SIZES  = true;  // true = run 1, 10, 100, 1000 in a row and print a comparison table
const bool PRINT_DETAILS  = true;   // true = print the full stats for the first datapoint

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
const float FREQ_SEARCH_PERCENT      = 2.0f;    // sine-fit search width around the zero-crossing estimate
const float MIN_AC_FREQ_HZ           = 1.0f;
const float MAX_RELIABLE_FREQ_HZ     = 300.0f;
const float MIN_SIGNAL_PEAK_V        = 0.5f;    // below this the voltage channel is treated as "no signal"
const int   NUMBER_OF_HARMONICS      = 15;      // upper limit; also capped by 300 Hz and Nyquist

// Calibration offsets (stub for the "clean up" stage; subtracted before analysis)
const float V_OFFSET = 0.0f;
const float I_OFFSET = 0.0f;

// ===========================================================================
//  SYNTHETIC DATA SETTINGS (fake signal so the code runs without hardware)
// ===========================================================================
const float SIM_VOLTAGE_PEAK_V   = 28.0f;
const float SIM_CURRENT_PEAK_A   = 10.0f;
const float SIM_FREQUENCY_HZ     = 60.2f;   // slightly off 60 to exercise the frequency estimate
const float SIM_CURRENT_LAG_DEG  = 30.0f;   // current lags voltage -> power factor ~0.87
const float SIM_V_HARMONIC3      = 0.02f;   // 3rd harmonic, fraction of fundamental (2%)
const float SIM_I_HARMONIC3      = 0.15f;   // 3rd harmonic, fraction of fundamental (15%)
const float SIM_NOISE_V          = 0.10f;   // uniform noise amplitude, volts
const float SIM_NOISE_I          = 0.05f;   // uniform noise amplitude, amps
const uint32_t RNG_SEED          = 12345;

// ===========================================================================
//  INTERNALS
// ===========================================================================
static_assert(NUMBER_OF_HARMONICS >= 1, "need at least 1 harmonic");
constexpr int MAXP = 2 * NUMBER_OF_HARMONICS + 1;   // max unknowns in the least-squares fit

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

struct BenchStats {
  int      count;
  double   totalMs;
  double   avgUs;
  uint32_t minUs, maxUs;
  double   energyWh;
};

// Big work arrays are global (not on the stack) because the Arduino loop task
// only has ~8 KB of stack.
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
//  Basis order: [1, sin1, cos1, sin2, cos2, ...]  (same as the Python code)
//  sin/cos values are advanced by a rotation recurrence, which avoids calling
//  sinf/cosf for every sample and is much faster on the ESP32.
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
  const double w = 2.0 * M_PI * (double)f / (double)SAMPLES_PER_SECOND;
  for (int h = 1; h <= H; h++) {
    sd[h] = (float)sin(h * w);
    cd[h] = (float)cos(h * w);
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
  const double w = 2.0 * M_PI * (double)f / (double)SAMPLES_PER_SECOND;
  const float sd = (float)sin(w), cd = (float)cos(w);
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

// Golden-section search for the frequency with the lowest sine-fit error.
static float refineFrequency(float f0, const float* y) {
  float a = f0 * (1.0f - FREQ_SEARCH_PERCENT / 100.0f);
  float b = f0 * (1.0f + FREQ_SEARCH_PERCENT / 100.0f);
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

// Waveform residual as % of the signal's AC RMS, after subtracting the fitted
// harmonic model. Catches noise, spikes and non-harmonic distortion.
static float residualPercent(float f, int H, const float* coef, const float* x) {
  const double w = 2.0 * M_PI * (double)f / (double)SAMPLES_PER_SECOND;
  float sd[NUMBER_OF_HARMONICS + 1], cd[NUMBER_OF_HARMONICS + 1];
  float s[NUMBER_OF_HARMONICS + 1],  c[NUMBER_OF_HARMONICS + 1];
  for (int h = 1; h <= H; h++) { sd[h] = (float)sin(h * w); cd[h] = (float)cos(h * w); s[h] = 0.0f; c[h] = 1.0f; }
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

  // --- 1. Clean up (stub: offset removal only; low-pass and gap checks go here later)
  for (int n = 0; n < N; n++) { vw[n] = vIn[n] - V_OFFSET; iw[n] = iIn[n] - I_OFFSET; }

  // --- 2. RMS, power, energy, power factor
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
  r.energyWh = (double)r.preal * (double)dtSec / 3600.0;   // Wh (Python printed kWh)

  // --- 3. Simple limit checks
  const float vHi = ALLOWABLE_VOLTAGE_V * (1.0f + VOLTAGE_TOLERANCE_PCT / 100.0f);
  const float vLo = ALLOWABLE_VOLTAGE_V * (1.0f - VOLTAGE_TOLERANCE_PCT / 100.0f);
  if (r.vrms > vHi) r.flags |= F_OVERVOLT;
  else if (r.vrms < vLo) r.flags |= F_UNDERVOLT;
  if (r.irms > ALLOWABLE_CURRENT_A) r.flags |= F_OVERCURRENT;

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

  // --- 5. Fine frequency from a sine fit
  r.freq = refineFrequency(r.freqZeroCross, vw);
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

static void generateDatapoint(int index, float* v, float* i) {
  const double w = 2.0 * M_PI * (double)SIM_FREQUENCY_HZ;
  const float lag = SIM_CURRENT_LAG_DEG * (float)M_PI / 180.0f;
  for (int n = 0; n < SAMPLES_PER_DATAPOINT; n++) {
    // Double precision + fmod keeps the phase accurate for large sample counts
    const double t = ((double)index * SAMPLES_PER_DATAPOINT + n) / (double)SAMPLES_PER_SECOND;
    const float ph = (float)fmod(w * t, 2.0 * M_PI);
    v[n] = SIM_VOLTAGE_PEAK_V * (sinf(ph) + SIM_V_HARMONIC3 * sinf(3.0f * ph)) + SIM_NOISE_V * randSym();
    i[n] = SIM_CURRENT_PEAK_A * (sinf(ph - lag) + SIM_I_HARMONIC3 * sinf(3.0f * ph)) + SIM_NOISE_I * randSym();
  }
}

// ---------------------------------------------------------------------------
//  PRINTING
// ---------------------------------------------------------------------------
static void printResult(const Result& r) {
  Serial.println(F("  --- Datapoint stats ---"));
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

// What the synthetic signal should give, so you can sanity-check the output.
static void printExpected() {
  const float lag = SIM_CURRENT_LAG_DEG * (float)M_PI / 180.0f;
  const float vr = SIM_VOLTAGE_PEAK_V / 1.41421356f * sqrtf(1.0f + SIM_V_HARMONIC3 * SIM_V_HARMONIC3);
  const float ir = SIM_CURRENT_PEAK_A / 1.41421356f * sqrtf(1.0f + SIM_I_HARMONIC3 * SIM_I_HARMONIC3);
  const float p  = 0.5f * SIM_VOLTAGE_PEAK_V * SIM_CURRENT_PEAK_A * cosf(lag)
                 + 0.5f * (SIM_VOLTAGE_PEAK_V * SIM_V_HARMONIC3) * (SIM_CURRENT_PEAK_A * SIM_I_HARMONIC3);
  Serial.println(F("  --- Expected from the synthetic signal (noise causes small differences) ---"));
  Serial.printf("  Vrms %.4f V | Irms %.4f A | P %.4f W | f %.4f Hz | THD_v %.2f %% | THD_i %.2f %%\n",
                vr, ir, p, SIM_FREQUENCY_HZ, SIM_V_HARMONIC3 * 100.0f, SIM_I_HARMONIC3 * 100.0f);
}

// ---------------------------------------------------------------------------
//  BENCHMARK RUNNER
// ---------------------------------------------------------------------------
static BenchStats runBatch(int count, bool verbose) {
  rngState = RNG_SEED;   // same fake data every run, so runs are comparable
  BenchStats st;
  st.count = count; st.minUs = 0xFFFFFFFFu; st.maxUs = 0; st.energyWh = 0.0;
  uint64_t totalUs = 0;
  static Result res;

  for (int d = 0; d < count; d++) {
    generateDatapoint(d, vRaw, iRaw);          // NOT timed

    const uint32_t t0 = micros();
    analyzeDatapoint(vRaw, iRaw, res);         // timed
    const uint32_t dt = micros() - t0;

    totalUs += dt;
    if (dt < st.minUs) st.minUs = dt;
    if (dt > st.maxUs) st.maxUs = dt;
    st.energyWh += res.energyWh;

    if (verbose && d == 0 && PRINT_DETAILS) {  // printing happens outside the timed section
      printExpected();
      printResult(res);
    }
    yield();
  }
  st.totalMs = totalUs / 1000.0;
  st.avgUs   = (double)totalUs / (double)count;
  return st;
}

static void printBatchSummary(const BenchStats& st) {
  const double dpUs = 1e6 * (double)SAMPLES_PER_DATAPOINT / (double)SAMPLES_PER_SECOND;
  Serial.println(F("  --- Timing ---"));
  Serial.printf("  Datapoints analysed   : %d\n", st.count);
  Serial.printf("  Total analysis time   : %.3f ms\n", st.totalMs);
  Serial.printf("  Average per datapoint : %.1f us\n", st.avgUs);
  Serial.printf("  Fastest / slowest     : %lu us / %lu us\n", (unsigned long)st.minUs, (unsigned long)st.maxUs);
  Serial.printf("  Signal time per point : %.2f ms (%d samples at %d S/s)\n", dpUs / 1000.0, SAMPLES_PER_DATAPOINT, SAMPLES_PER_SECOND);
  Serial.printf("  Real-time CPU load    : %.2f %% (analysis time / signal time)\n", 100.0 * st.avgUs / dpUs);
  Serial.printf("  Energy accumulated    : %.6f Wh\n", st.energyWh);
}

static void runRequested() {
  Serial.println();
  Serial.println(F("============================================================"));
  Serial.println(F(" PowerEye analysis benchmark"));
  Serial.println(F("============================================================"));
  Serial.printf(" CPU: %d MHz | %d samples/datapoint | %d cycles/datapoint\n",
                (int)getCpuFrequencyMhz(), SAMPLES_PER_DATAPOINT, AC_CYCLES_PER_DATAPOINT);

  if (RUN_ALL_SIZES) {
    const int sizes[4] = {1, 10, 100, 1000};
    BenchStats results[4];
    for (int k = 0; k < 4; k++) {
      Serial.printf(" Running %d datapoint(s)...\n", sizes[k]);
      results[k] = runBatch(sizes[k], k == 0);   // details printed once, for the 1-datapoint run
    }
    Serial.println();
    Serial.println(F(" Datapoints | Total (ms) | Avg/point (us) | Min (us) | Max (us)"));
    Serial.println(F(" -----------+------------+----------------+----------+---------"));
    for (int k = 0; k < 4; k++)
      Serial.printf(" %10d | %10.3f | %14.1f | %8lu | %8lu\n", results[k].count, results[k].totalMs,
                    results[k].avgUs, (unsigned long)results[k].minUs, (unsigned long)results[k].maxUs);
  } else {
    Serial.printf(" Running %d datapoint(s)...\n", NUM_DATAPOINTS);
    BenchStats st = runBatch(NUM_DATAPOINTS, true);
    printBatchSummary(st);
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
