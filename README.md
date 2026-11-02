# PowerEye signal-analysis core

The `src/` library analyzes one `RawFrame` at a time and has no runtime dependencies. It uses standard typed arrays and ECMAScript APIs only, so the analysis core can run in browser or Node environments. The existing Python `ingest.py` remains independent; device or transport code can adapt its data to `RawFrame`.

## Development

- `npm test` runs the Vitest suite.
- `npm run typecheck` runs strict TypeScript checking.
- `npm run build` emits ESM JavaScript and declarations into `dist/`.
- `npm run synthetic` prints measurements for seeded scenarios.

`RawFrameSource` is the input boundary. `CsvReplaySource` accepts one sample per CSV row with the header `nodeId,seq,tStartMs,sampleRateHz,vCounts,iCounts`; an optional `frameId` column preserves repeated frames with identical metadata. `SyntheticSource` replays generated frames. `PowerEyePipeline.process(frame)` returns a `WindowResult`. A pipeline instance maintains sequence/debounce/energy state; use a new instance or call `reset()` to begin another stream. Pipeline stages can be adjusted with `PipelineConfig` (calibration, cycle alignment, frequency fit, filtering, and detection switches; set `harmonics` to zero to skip harmonic calculation). `aggregateSummary()` reduces an interval of window results for the summary codec.

## Assumptions to replace with hardware facts

All hardware constants and threshold defaults are in [src/config.ts](./src/config.ts). Current placeholders are a signed 24-bit ADC, 1.2 V reference, 4 ksample/s, PGA gain 1, 499:1 voltage divider, isolation gains 1, and 2 mOhm shunt. Default line values are 120 V RMS / 60 Hz. Replace isolation gains, divider/shunt tolerances, offsets, gain trims, sampling rate, current limit, and detection limits after calibration and device characterization.

`tStartMs` is the epoch timestamp in milliseconds of the first sample, or a monotonic timestamp used consistently by the source. The raw-frame schema and CSV format are provisional. The packet format is also provisional and isolated in [src/packets.ts](./src/packets.ts).

## Packet quantization

The 24-byte little-endian summary packet rounds its seconds timestamp to the nearest second (maximum error 0.5 s), voltage to 0.01 V (0.005 V), current to 1 mA (0.5 mA), frequency to 1 mHz (0.5 mHz), real/apparent power to 0.1 unit (0.05 W/VA), voltage THD to 0.01 percentage point (0.005 point), and energy to 0.1 Wh (0.05 Wh). Battery percentage rounds to 1 percentage point (0.5 point); flags and sequence are integer bit/sequence fields. Fields saturate at their unsigned wire maximum; the provisional unsigned real-power field encodes negative power as zero. EVENT peak voltage uses 0.01 V and peak current 1 mA; waveform samples are signed 16-bit V/I pairs and the provisional packet allows at most 64 pairs.

The summary packet carries voltage THD only. Packet layouts, scaling, signedness, and timestamp semantics must be checked against the real protocol before deployment.