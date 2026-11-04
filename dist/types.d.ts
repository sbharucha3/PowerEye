export interface RawFrame {
    nodeId: number;
    seq: number;
    /** Unix milliseconds of the first sample (or a consistently monotonic epoch). */
    tStartMs: number;
    sampleRateHz: number;
    vCounts: Int32Array;
    iCounts: Int32Array;
    batteryPercent?: number;
}
export interface CalibratedFrame {
    readonly raw: RawFrame;
    readonly voltage: Float64Array;
    readonly current: Float64Array;
}
export type FrameSequenceStatus = "first" | "in_order" | "gap" | "duplicate" | "out_of_order";
export type WindowValidity = "ok" | "too_few_cycles" | "clipped" | "no_signal";
export interface HarmonicResult {
    /** Peak amplitude (same units as the source channel), indexed from harmonic 1. */
    amplitude: Float64Array;
    /** Phase in radians, relative to the window's first sample. */
    phase: Float64Array;
}
export interface WindowResult {
    nodeId: number;
    seq: number;
    tStartMs: number;
    durationMs: number;
    validity: WindowValidity;
    sequenceStatus: FrameSequenceStatus;
    gapFrames: number;
    vrms: number;
    irms: number;
    pReal: number;
    sApparent: number;
    qReactive: number;
    powerFactor: number;
    freqZeroCross: number | null;
    freqFit: number | null;
    freqHz: number | null;
    thdV: number | null;
    thdI: number | null;
    harmonicsV: Float64Array;
    harmonicsI: Float64Array;
    harmonicPhaseV: Float64Array;
    harmonicPhaseI: Float64Array;
    vPeak: number;
    iPeak: number;
    crestV: number;
    crestI: number;
    fitResidualRms: number;
    energyWh: number;
    flags: number;
    flagReasons: string[];
    batteryPercent: number | null;
}
export interface SummaryPacket {
    timestampSeconds: number;
    vrms: number;
    irms: number;
    frequencyHz: number;
    pReal: number;
    sApparent: number;
    thdVPercent: number;
    energyWh: number;
    flags: number;
    batteryPercent: number;
    seq: number;
}
export interface EventPacket {
    type: number;
    nodeId: number;
    timestampSeconds: number;
    flags: number;
    peakVoltage: number;
    peakCurrent: number;
    /** Interleaved signed 16-bit V/I samples; maximum 64 pairs. */
    waveform?: Int16Array;
}
export interface SyntheticGroundTruth {
    vrms: number;
    irms: number;
    pReal: number;
    frequencyHz: number;
    thdVPercent: number;
    thdIPercent: number;
}
export interface SyntheticScenario {
    name: string;
    frames: RawFrame[];
    groundTruth: SyntheticGroundTruth;
}
export declare enum Flag {
    Voltage = 1,
    VoltageSag = 2,
    VoltageSwell = 4,
    Current = 8,
    Frequency = 16,
    Anomaly = 32,
    Clipped = 64,
    DataGap = 128
}
