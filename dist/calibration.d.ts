import type { CalibratedFrame, RawFrame } from "./types.js";
import type { DeviceConfig } from "./config.js";
export declare function countsToVoltage(counts: number, config: DeviceConfig): number;
export declare function countsToCurrent(counts: number, config: DeviceConfig): number;
export declare function calibrateFrame(frame: RawFrame, config: DeviceConfig, autoEstimateDcOffset?: boolean): CalibratedFrame;
