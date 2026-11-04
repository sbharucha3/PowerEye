import type { PipelineConfig } from "./config.js";
export interface DetectionInput {
    vrms: number;
    irms: number;
    frequencyHz: number | null;
    thdV: number | null;
    thdI: number | null;
    crestV: number;
    crestI: number;
    fitResidualRms: number;
    maximumFitResidual: number;
    clipped: boolean;
}
export interface DetectionOutput {
    flags: number;
    reasons: string[];
}
export declare function detectConditions(input: DetectionInput, config: PipelineConfig): DetectionOutput;
export declare class FlagDebouncer {
    private readonly setWindows;
    private readonly clearWindows;
    private readonly active;
    private readonly setCounts;
    private readonly clearCounts;
    constructor(setWindows: number, clearWindows: number);
    reset(): void;
    update(candidateFlags: number): number;
}
export declare function flagReasonsForFlags(reasons: string[], flags: number): string[];
