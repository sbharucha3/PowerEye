import { type PipelineConfig, type PipelineConfigOverrides } from "./config.js";
import { type RawFrame, type WindowResult } from "./types.js";
export declare class PowerEyePipeline {
    private readonly config;
    private readonly energy;
    private readonly debouncer;
    private lastSequence;
    private lastStartMs;
    private lastDurationMs;
    constructor(config?: PipelineConfigOverrides);
    getConfig(): PipelineConfig;
    reset(): void;
    process(frame: RawFrame): WindowResult;
    private trackSequence;
}
export declare function processFrame(frame: RawFrame, config?: PipelineConfigOverrides): WindowResult;
