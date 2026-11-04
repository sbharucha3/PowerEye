import { makePipelineConfig } from "./config.js";
import { PowerEyePipeline } from "./pipeline.js";
export function compareFilterOnOff(frame, cutoffHz, config = {}) {
    const base = makePipelineConfig(config);
    const withoutFilter = new PowerEyePipeline({ ...base, lowPassEnabled: false }).process(frame);
    const withFilter = new PowerEyePipeline({ ...base, lowPassEnabled: true, lowPassCutoffHz: cutoffHz }).process(frame);
    const deltas = {};
    const fields = [
        "durationMs", "vrms", "irms", "pReal", "sApparent", "qReactive", "powerFactor",
        "freqZeroCross", "freqFit", "freqHz", "thdV", "thdI", "vPeak", "iPeak",
        "crestV", "crestI", "fitResidualRms", "energyWh", "flags", "batteryPercent",
    ];
    for (const field of fields) {
        const before = withoutFilter[field];
        const after = withFilter[field];
        if (typeof before === "number" && typeof after === "number")
            deltas[field] = after - before;
        else if (before === null && after === null)
            deltas[field] = 0;
        else
            deltas[field] = null;
    }
    const arrayFields = ["harmonicsV", "harmonicsI", "harmonicPhaseV", "harmonicPhaseI"];
    for (const field of arrayFields) {
        const before = withoutFilter[field];
        const after = withFilter[field];
        const count = Math.max(before.length, after.length);
        for (let index = 0; index < count; index += 1) {
            deltas[`${field}.${index + 1}`] = (after[index] ?? 0) - (before[index] ?? 0);
        }
    }
    return { withoutFilter, withFilter, deltas };
}
