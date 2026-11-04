import { Flag } from "./types.js";
const REASON_BITS = Object.freeze({
    voltage: Flag.Voltage,
    voltage_sag: Flag.Voltage | Flag.VoltageSag,
    voltage_swell: Flag.Voltage | Flag.VoltageSwell,
    current: Flag.Current,
    frequency: Flag.Frequency,
    voltage_thd: Flag.Anomaly,
    current_thd: Flag.Anomaly,
    voltage_crest: Flag.Anomaly,
    current_crest: Flag.Anomaly,
    sine_residual: Flag.Anomaly,
    single_sample_spike: Flag.Anomaly,
    clipped: Flag.Clipped,
});
export function detectConditions(input, config) {
    const reasons = [];
    const nominal = config.device.nominalVoltageRms;
    if (input.vrms < nominal * (1 - config.voltageTolerancePercent / 100)
        || input.vrms > nominal * (1 + config.voltageTolerancePercent / 100))
        reasons.push("voltage");
    if (input.vrms < nominal * config.voltageSagPercent / 100)
        reasons.push("voltage_sag");
    if (input.vrms > nominal * config.voltageSwellPercent / 100)
        reasons.push("voltage_swell");
    if (input.irms > config.currentLimitAmps)
        reasons.push("current");
    if (input.frequencyHz !== null
        && Math.abs(input.frequencyHz - config.device.nominalFrequencyHz) > config.frequencyToleranceHz)
        reasons.push("frequency");
    if (input.thdV !== null && input.thdV > config.voltageThdLimitPercent)
        reasons.push("voltage_thd");
    if (input.thdI !== null && input.thdI > config.currentThdLimitPercent)
        reasons.push("current_thd");
    if (input.crestV > 0 && (input.crestV < config.crestFactorMin || input.crestV > config.crestFactorMax))
        reasons.push("voltage_crest");
    if (input.crestI > 0 && (input.crestI < config.crestFactorMin || input.crestI > config.crestFactorMax))
        reasons.push("current_crest");
    if (input.fitResidualRms > config.fitResidualLimitVolts)
        reasons.push("sine_residual");
    if (input.maximumFitResidual > config.spikeSigmaLimit * Math.max(input.fitResidualRms, 1e-9)) {
        reasons.push("single_sample_spike");
    }
    if (input.clipped)
        reasons.push("clipped");
    let flags = 0;
    for (const reason of reasons)
        flags |= REASON_BITS[reason] ?? 0;
    return { flags, reasons };
}
export class FlagDebouncer {
    setWindows;
    clearWindows;
    active = new Map();
    setCounts = new Map();
    clearCounts = new Map();
    constructor(setWindows, clearWindows) {
        this.setWindows = setWindows;
        this.clearWindows = clearWindows;
    }
    reset() {
        this.active.clear();
        this.setCounts.clear();
        this.clearCounts.clear();
    }
    update(candidateFlags) {
        // Set and clear transitions are counted independently so short excursions do not flicker flags.
        let output = 0;
        for (const bit of Object.values(Flag)) {
            if (typeof bit !== "number")
                continue;
            const candidate = (candidateFlags & bit) !== 0;
            const wasActive = this.active.get(bit) ?? false;
            if (candidate) {
                this.clearCounts.set(bit, 0);
                const count = (this.setCounts.get(bit) ?? 0) + 1;
                this.setCounts.set(bit, count);
                if (count >= Math.max(1, this.setWindows))
                    this.active.set(bit, true);
            }
            else {
                this.setCounts.set(bit, 0);
                const count = (this.clearCounts.get(bit) ?? 0) + 1;
                this.clearCounts.set(bit, count);
                if (count >= Math.max(1, this.clearWindows))
                    this.active.set(bit, false);
            }
            if (wasActive || (this.active.get(bit) ?? false)) {
                if (this.active.get(bit))
                    output |= bit;
            }
        }
        return output;
    }
}
export function flagReasonsForFlags(reasons, flags) {
    const activeReasons = reasons.filter((reason) => {
        const bit = REASON_BITS[reason] ?? 0;
        return (flags & bit) === bit;
    });
    let explainedFlags = Flag.DataGap;
    for (const reason of activeReasons)
        explainedFlags |= REASON_BITS[reason] ?? 0;
    if ((flags & Flag.DataGap) !== 0)
        activeReasons.push("data_gap");
    if ((flags & ~explainedFlags) !== 0)
        activeReasons.push("debounced");
    return activeReasons;
}
