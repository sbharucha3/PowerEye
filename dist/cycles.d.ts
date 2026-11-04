export interface CycleRegion {
    /** Interpolated sample-index positions of rising zero crossings. */
    crossings: Float64Array;
    startPosition: number;
    endPosition: number;
    startIndex: number;
    endIndex: number;
    cycleCount: number;
    frequencyHz: number | null;
}
export declare function findRisingZeroCrossings(signal: Float64Array, hysteresis?: number): Float64Array;
export declare function findCycleRegion(signal: Float64Array, sampleRateHz: number, hysteresis?: number): CycleRegion;
