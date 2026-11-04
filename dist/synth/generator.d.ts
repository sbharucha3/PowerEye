import { type PipelineConfigOverrides } from "../config.js";
import type { SyntheticScenario } from "../types.js";
export type ScenarioName = "normal" | "reactive-lagging" | "reactive-leading" | "nonlinear" | "sag" | "swell" | "sag-ramp" | "swell-ramp" | "overcurrent" | "frequency-step" | "frequency-wander" | "noise" | "dc-offset" | "gain-error" | "spike" | "transient" | "clipping" | "dropped-frame" | "duplicate-frame" | "out-of-order-frame";
export interface SyntheticOptions {
    seed?: number;
    sampleRateHz?: number;
    samplesPerFrame?: number;
    frameCount?: number;
    nodeId?: number;
    sequenceStart?: number;
    snrDb?: number;
    currentAmps?: number;
    voltageRms?: number;
    frequencyHz?: number;
    thdPercent?: number;
    config?: PipelineConfigOverrides;
}
export declare function createSyntheticScenario(name: ScenarioName, options?: SyntheticOptions): SyntheticScenario;
export declare function availableScenarios(): readonly ScenarioName[];
export declare const DEFAULT_SYNTHETIC_SAMPLE_RATE: number;
