import { makePipelineConfig } from "./config.js";
import { calibrateFrame } from "./calibration.js";
import { findCycleRegion } from "./cycles.js";
import { detectConditions, FlagDebouncer, flagReasonsForFlags } from "./detect.js";
import { applyBiquad, butterworthLowPassCoefficients } from "./filter.js";
import { fitSineFrequency } from "./frequency.js";
import { calculatePower, EnergyAccumulator } from "./power.js";
import { analyzeHarmonics, calculateThd } from "./spectrum.js";
import { Flag } from "./types.js";
function isClipped(frame, fullScale, headroom) {
    const limit = fullScale - headroom;
    for (let index = 0; index < frame.vCounts.length; index += 1) {
        if (Math.abs(frame.vCounts[index] ?? 0) >= limit)
            return true;
    }
    for (let index = 0; index < frame.iCounts.length; index += 1) {
        if (Math.abs(frame.iCounts[index] ?? 0) >= limit)
            return true;
    }
    return false;
}
function copyRegion(source, start, end) {
    const length = Math.max(0, end - start);
    const result = new Float64Array(length);
    for (let index = 0; index < length; index += 1)
        result[index] = source[start + index] ?? 0;
    return result;
}
function emptyResult(frame, sequenceStatus, gapFrames) {
    return {
        nodeId: frame.nodeId,
        seq: frame.seq,
        tStartMs: frame.tStartMs,
        durationMs: 0,
        validity: "no_signal",
        sequenceStatus,
        gapFrames,
        vrms: 0,
        irms: 0,
        pReal: 0,
        sApparent: 0,
        qReactive: 0,
        powerFactor: 0,
        freqZeroCross: null,
        freqFit: null,
        freqHz: null,
        thdV: null,
        thdI: null,
        harmonicsV: new Float64Array(0),
        harmonicsI: new Float64Array(0),
        harmonicPhaseV: new Float64Array(0),
        harmonicPhaseI: new Float64Array(0),
        vPeak: 0,
        iPeak: 0,
        crestV: 0,
        crestI: 0,
        fitResidualRms: 0,
        energyWh: 0,
        flags: 0,
        flagReasons: [],
        batteryPercent: frame.batteryPercent ?? null,
    };
}
export class PowerEyePipeline {
    config;
    energy = new EnergyAccumulator();
    debouncer;
    lastSequence = null;
    lastStartMs = null;
    lastDurationMs = 0;
    constructor(config = {}) {
        this.config = makePipelineConfig(config);
        this.debouncer = new FlagDebouncer(this.config.debounceWindows, this.config.debounceClearWindows);
    }
    getConfig() {
        return { ...this.config, device: { ...this.config.device } };
    }
    reset() {
        this.energy.reset();
        this.debouncer.reset();
        this.lastSequence = null;
        this.lastStartMs = null;
        this.lastDurationMs = 0;
    }
    process(frame) {
        const length = Math.min(frame.vCounts.length, frame.iCounts.length);
        const durationMs = frame.sampleRateHz > 0 ? (length / frame.sampleRateHz) * 1000 : 0;
        // Sequence tracking happens before analysis so duplicate/out-of-order frames cannot double-count energy.
        const { status, gapFrames, missingByTime } = this.trackSequence(frame, durationMs);
        if (length === 0 || !(frame.sampleRateHz > 0)) {
            return {
                ...emptyResult(frame, status, gapFrames),
                gapFrames: gapFrames + (missingByTime && gapFrames === 0 ? 1 : 0),
                energyWh: this.energy.value(),
                flags: gapFrames > 0 || missingByTime ? Flag.DataGap : 0,
                flagReasons: gapFrames > 0 || missingByTime ? ["data_gap"] : [],
            };
        }
        const calibrated = this.config.calibrationEnabled
            ? calibrateFrame(frame, this.config.device, this.config.autoEstimateDcOffset)
            : {
                raw: frame,
                voltage: Float64Array.from(frame.vCounts.subarray(0, length)),
                current: Float64Array.from(frame.iCounts.subarray(0, length)),
            };
        const unfilteredVoltage = calibrated.voltage;
        const unfilteredCurrent = calibrated.current;
        let voltage = unfilteredVoltage;
        let current = unfilteredCurrent;
        if (this.config.lowPassEnabled) {
            const coefficients = butterworthLowPassCoefficients(frame.sampleRateHz, this.config.lowPassCutoffHz);
            voltage = applyBiquad(unfilteredVoltage, coefficients);
            current = applyBiquad(unfilteredCurrent, coefficients);
        }
        const cycleRegion = findCycleRegion(voltage, frame.sampleRateHz, this.config.zeroCrossHysteresisVolts);
        const enoughCycles = !this.config.cycleAlignmentEnabled
            || cycleRegion.cycleCount >= this.config.minimumCycles;
        const start = this.config.cycleAlignmentEnabled && enoughCycles ? cycleRegion.startIndex : 0;
        const end = this.config.cycleAlignmentEnabled && enoughCycles ? cycleRegion.endIndex : length;
        const metrics = calculatePower(voltage, current, start, end);
        const fitInput = copyRegion(voltage, start, end);
        const fit = this.config.frequencyFitEnabled ? fitSineFrequency(fitInput, frame.sampleRateHz, cycleRegion.frequencyHz, this.config.frequencySearchRangeHz, this.config.frequencySearchSteps) : { frequencyHz: null, amplitude: 0, phaseRadians: 0, offset: 0, residualRms: 0 };
        const frequencyHz = fit.frequencyHz ?? cycleRegion.frequencyHz;
        // Preserve unfiltered samples for distortion analysis unless callers explicitly choose otherwise.
        const harmonicVSignal = this.config.harmonicsUseUnfiltered ? unfilteredVoltage : voltage;
        const harmonicISignal = this.config.harmonicsUseUnfiltered ? unfilteredCurrent : current;
        const harmonicV = analyzeHarmonics(harmonicVSignal, frame.sampleRateHz, frequencyHz ?? 0, this.config.harmonics, start, end);
        const harmonicI = analyzeHarmonics(harmonicISignal, frame.sampleRateHz, frequencyHz ?? 0, this.config.harmonics, start, end);
        const thdV = calculateThd(harmonicV.amplitude, this.config.minimumThdFundamental);
        const thdI = calculateThd(harmonicI.amplitude, this.config.minimumThdFundamental);
        let maximumFitResidual = 0;
        if (fit.frequencyHz !== null) {
            for (let index = 0; index < fitInput.length; index += 1) {
                const angle = (2 * Math.PI * fit.frequencyHz * index) / frame.sampleRateHz;
                const predicted = fit.offset + fit.amplitude * Math.sin(angle + fit.phaseRadians);
                maximumFitResidual = Math.max(maximumFitResidual, Math.abs((fitInput[index] ?? 0) - predicted));
            }
        }
        const clipped = isClipped(frame, this.config.device.adcFullScaleCounts, this.config.clippingHeadroomCounts);
        const noSignal = metrics.vrms < this.config.minimumSignalRms;
        const validity = clipped ? "clipped" : noSignal ? "no_signal" : enoughCycles ? "ok" : "too_few_cycles";
        const detection = this.config.detectionEnabled ? detectConditions({
            vrms: metrics.vrms,
            irms: metrics.irms,
            frequencyHz,
            thdV,
            thdI,
            crestV: metrics.crestV,
            crestI: metrics.crestI,
            fitResidualRms: fit.residualRms,
            maximumFitResidual,
            clipped,
        }, this.config) : { flags: 0, reasons: [] };
        const processable = status === "first" || status === "in_order" || status === "gap";
        // A gap is reported but only this frame's analyzed duration is integrated, never the missing time.
        const energyWh = this.energy.add(metrics.pReal, processable && validity === "ok" ? (end - start) / frame.sampleRateHz : 0);
        let flags = this.debouncer.update(detection.flags);
        if (gapFrames > 0 || missingByTime)
            flags |= Flag.DataGap;
        const flagReasons = flagReasonsForFlags(detection.reasons, flags);
        return {
            nodeId: frame.nodeId,
            seq: frame.seq,
            tStartMs: frame.tStartMs,
            durationMs: Math.max(0, ((end - start) / frame.sampleRateHz) * 1000),
            validity,
            sequenceStatus: status,
            gapFrames: gapFrames + (missingByTime && gapFrames === 0 ? 1 : 0),
            vrms: metrics.vrms,
            irms: metrics.irms,
            pReal: metrics.pReal,
            sApparent: metrics.sApparent,
            qReactive: metrics.qReactive,
            powerFactor: metrics.powerFactor,
            freqZeroCross: cycleRegion.frequencyHz,
            freqFit: fit.frequencyHz,
            freqHz: frequencyHz,
            thdV,
            thdI,
            harmonicsV: harmonicV.amplitude,
            harmonicsI: harmonicI.amplitude,
            harmonicPhaseV: harmonicV.phase,
            harmonicPhaseI: harmonicI.phase,
            vPeak: metrics.vPeak,
            iPeak: metrics.iPeak,
            crestV: metrics.crestV,
            crestI: metrics.crestI,
            fitResidualRms: fit.residualRms,
            energyWh,
            flags,
            flagReasons,
            batteryPercent: frame.batteryPercent ?? null,
        };
    }
    trackSequence(frame, durationMs) {
        if (this.lastSequence === null) {
            this.lastSequence = frame.seq & 0xffff;
            this.lastStartMs = frame.tStartMs;
            this.lastDurationMs = durationMs;
            return { status: "first", gapFrames: 0, missingByTime: false };
        }
        const difference = ((frame.seq & 0xffff) - this.lastSequence + 0x10000) % 0x10000;
        let status;
        let gapFrames = 0;
        if (difference === 0)
            status = "duplicate";
        else if (difference >= 0x8000)
            status = "out_of_order";
        else if (difference > 1) {
            status = "gap";
            gapFrames = difference - 1;
        }
        else
            status = "in_order";
        const elapsed = this.lastStartMs === null ? 0 : frame.tStartMs - (this.lastStartMs + this.lastDurationMs);
        const tolerance = (this.config.timestampToleranceSamples / Math.max(frame.sampleRateHz, 1)) * 1000;
        const missingByTime = status === "in_order" && elapsed > tolerance;
        if (status === "in_order" || status === "gap") {
            this.lastSequence = frame.seq & 0xffff;
            this.lastStartMs = frame.tStartMs;
            this.lastDurationMs = durationMs;
        }
        return { status, gapFrames, missingByTime };
    }
}
export function processFrame(frame, config = {}) {
    return new PowerEyePipeline(config).process(frame);
}
