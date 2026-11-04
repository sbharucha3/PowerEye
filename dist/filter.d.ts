export interface BiquadCoefficients {
    b0: number;
    b1: number;
    b2: number;
    a1: number;
    a2: number;
}
export declare function butterworthLowPassCoefficients(sampleRateHz: number, cutoffHz: number): BiquadCoefficients;
export declare function applyBiquad(signal: Float64Array, coefficients: BiquadCoefficients): Float64Array;
