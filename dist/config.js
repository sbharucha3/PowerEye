export const DEFAULT_DEVICE_CONFIG = Object.freeze({
    adcFullScaleCounts: 2 ** 23,
    adcReferenceVolts: 1.2,
    pgaGain: 1,
    voltageDividerRatio: 499,
    voltageIsolationGain: 1,
    currentIsolationGain: 1,
    shuntOhms: 0.002,
    voltageOffset: 0,
    currentOffset: 0,
    voltageGainTrim: 1,
    currentGainTrim: 1,
    sampleRateHz: 4000,
    nominalVoltageRms: 120,
    nominalFrequencyHz: 60,
    defaultCurrentLimitAmps: 15,
});
export const DEFAULT_PIPELINE_CONFIG = Object.freeze({
    device: DEFAULT_DEVICE_CONFIG,
    calibrationEnabled: true,
    autoEstimateDcOffset: false,
    cycleAlignmentEnabled: true,
    frequencyFitEnabled: true,
    detectionEnabled: true,
    lowPassEnabled: false,
    // Keep the 60 Hz waveform and useful low-order harmonics while suppressing high-frequency noise.
    lowPassCutoffHz: 500,
    harmonicsUseUnfiltered: true,
    harmonics: 31,
    // Two complete cycles yield stable estimates while keeping the default 1 s windows efficient.
    minimumCycles: 2,
    zeroCrossHysteresisVolts: 1,
    frequencySearchRangeHz: 2,
    frequencySearchSteps: 200,
    clippingHeadroomCounts: 8,
    voltageTolerancePercent: 5,
    // Sag/swell classification is intentionally independent of the tighter voltage warning band.
    voltageSagPercent: 90,
    voltageSwellPercent: 110,
    currentLimitAmps: DEFAULT_DEVICE_CONFIG.defaultCurrentLimitAmps,
    frequencyToleranceHz: 0.5,
    voltageThdLimitPercent: 8,
    currentThdLimitPercent: 10,
    // Avoid undefined THD for absent/negligible fundamentals; tune to calibrated ADC noise floors.
    minimumThdFundamental: 1e-9,
    crestFactorMin: 1.2,
    crestFactorMax: 1.65,
    fitResidualLimitVolts: 2,
    spikeSigmaLimit: 8,
    debounceWindows: 1,
    debounceClearWindows: 1,
    minimumSignalRms: 0.01,
    timestampToleranceSamples: 4,
});
export function makePipelineConfig(overrides = {}) {
    const config = {
        ...DEFAULT_PIPELINE_CONFIG,
        ...overrides,
        device: { ...DEFAULT_DEVICE_CONFIG, ...overrides.device },
    };
    const positiveDeviceValues = [
        config.device.adcFullScaleCounts,
        config.device.adcReferenceVolts,
        config.device.pgaGain,
        config.device.voltageDividerRatio,
        config.device.voltageIsolationGain,
        config.device.currentIsolationGain,
        config.device.shuntOhms,
        config.device.voltageGainTrim,
        config.device.currentGainTrim,
        config.device.sampleRateHz,
        config.device.nominalVoltageRms,
        config.device.nominalFrequencyHz,
        config.device.defaultCurrentLimitAmps,
    ];
    if (positiveDeviceValues.some((value) => !Number.isFinite(value) || value <= 0)
        || !Number.isFinite(config.device.voltageOffset)
        || !Number.isFinite(config.device.currentOffset)) {
        throw new RangeError("Device calibration and nominal values must be finite and positive where required.");
    }
    if (!Number.isInteger(config.harmonics) || config.harmonics < 0
        || !Number.isInteger(config.minimumCycles) || config.minimumCycles < 1
        || !Number.isInteger(config.frequencySearchSteps) || config.frequencySearchSteps < 3
        || !Number.isInteger(config.debounceWindows) || config.debounceWindows < 1
        || !Number.isInteger(config.debounceClearWindows) || config.debounceClearWindows < 1
        || !Number.isFinite(config.clippingHeadroomCounts)
        || config.clippingHeadroomCounts < 0
        || config.clippingHeadroomCounts >= config.device.adcFullScaleCounts) {
        throw new RangeError("Pipeline counts and window thresholds are outside their valid ranges.");
    }
    const nonNegativeThresholds = [
        config.zeroCrossHysteresisVolts,
        config.frequencySearchRangeHz,
        config.voltageTolerancePercent,
        config.voltageSagPercent,
        config.voltageSwellPercent,
        config.currentLimitAmps,
        config.frequencyToleranceHz,
        config.voltageThdLimitPercent,
        config.currentThdLimitPercent,
        config.minimumThdFundamental,
        config.crestFactorMin,
        config.crestFactorMax,
        config.fitResidualLimitVolts,
        config.spikeSigmaLimit,
        config.minimumSignalRms,
        config.timestampToleranceSamples,
    ];
    if (nonNegativeThresholds.some((value) => !Number.isFinite(value) || value < 0)
        || config.crestFactorMax < config.crestFactorMin
        || (config.lowPassEnabled
            && (!Number.isFinite(config.lowPassCutoffHz)
                || config.lowPassCutoffHz <= 0
                || config.lowPassCutoffHz >= config.device.sampleRateHz / 2))) {
        throw new RangeError("Pipeline thresholds and filter cutoff must be finite and within valid ranges.");
    }
    return config;
}
