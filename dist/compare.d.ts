import { type PipelineConfigOverrides } from "./config.js";
import type { RawFrame, WindowResult } from "./types.js";
export interface FilterComparison {
    withoutFilter: WindowResult;
    withFilter: WindowResult;
    deltas: Readonly<Record<string, number | null>>;
}
export declare function compareFilterOnOff(frame: RawFrame, cutoffHz: number, config?: PipelineConfigOverrides): FilterComparison;
