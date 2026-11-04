export interface DeviceConfig {
    /** ASSUMPTION: ADS131M02 signed 24-bit full-scale code. */
    adcFullScaleCounts: number;
    /** ASSUMPTION: ADS131M02 reference voltage. */
    adcReferenceVolts: number;
    /** ASSUMPTION: per-channel programmable gain setting. */
    pgaGain: number;
    /** ASSUMPTION: divider is (4*249 kOhm + 2 kOhm) / 2 kOhm. */
    voltageDividerRatio: number;
    /** ASSUMPTION: isolation amplifier gains; replace with measured values. */
    voltageIsolationGain: number;
    currentIsolationGain: number;
    /** ASSUMPTION: shunt resistance on the hot leg. */
    shuntOhms: number;
    voltageOffset: number;
    currentOffset: number;
    voltageGainTrim: number;
    currentGainTrim: number;
    sampleRateHz: number;
    nominalVoltageRms: number;
    nominalFrequencyHz: number;
    defaultCurrentLimitAmps: number;
}
export interface PipelineConfig {
    device: DeviceConfig;
    calibrationEnabled: boolean;
    autoEstimateDcOffset: boolean;
    cycleAlignmentEnabled: boolean;
    frequencyFitEnabled: boolean;
    detectionEnabled: boolean;
    lowPassEnabled: boolean;
    lowPassCutoffHz: number;
    harmonicsUseUnfiltered: boolean;
    harmonics: number;
    minimumCycles: number;
    zeroCrossHysteresisVolts: number;
    frequencySearchRangeHz: number;
    frequencySearchSteps: number;
    clippingHeadroomCounts: number;
    voltageTolerancePercent: number;
    voltageSagPercent: number;
    voltageSwellPercent: number;
    currentLimitAmps: number;
    frequencyToleranceHz: number;
    voltageThdLimitPercent: number;
    currentThdLimitPercent: number;
    minimumThdFundamental: number;
    crestFactorMin: number;
    crestFactorMax: number;
    fitResidualLimitVolts: number;
    spikeSigmaLimit: number;
    debounceWindows: number;
    debounceClearWindows: number;
    minimumSignalRms: number;
    /** Tolerance in sample periods; covers integer-millisecond timestamp quantization. */
    timestampToleranceSamples: number;
}
export type PipelineConfigOverrides = Omit<Partial<PipelineConfig>, "device"> & {
    device?: Partial<DeviceConfig>;
};
export declare const DEFAULT_DEVICE_CONFIG: Readonly<DeviceConfig>;
export declare const DEFAULT_PIPELINE_CONFIG: Readonly<PipelineConfig>;
export declare function makePipelineConfig(overrides?: PipelineConfigOverrides): PipelineConfig;
