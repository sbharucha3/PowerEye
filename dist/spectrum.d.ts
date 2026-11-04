import type { HarmonicResult } from "./types.js";
export declare function analyzeHarmonics(signal: Float64Array, sampleRateHz: number, fundamentalHz: number, requestedHarmonics: number, startIndex?: number, endIndex?: number): HarmonicResult;
export declare function calculateThd(harmonics: Float64Array, minimumFundamental: number): number | null;
