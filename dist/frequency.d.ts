export interface SineFit {
    frequencyHz: number | null;
    amplitude: number;
    phaseRadians: number;
    offset: number;
    residualRms: number;
}
export declare function fitSineFrequency(signal: Float64Array, sampleRateHz: number, initialFrequencyHz: number | null, searchRangeHz?: number, searchSteps?: number): SineFit;
