export function countsToVoltage(counts, config) {
    // Reverse ADC gain and isolation first, then apply the sensor's divider and calibration trim.
    const adcVolts = (counts * config.adcReferenceVolts) / config.adcFullScaleCounts / config.pgaGain;
    return ((adcVolts / config.voltageIsolationGain) * config.voltageDividerRatio - config.voltageOffset)
        * config.voltageGainTrim;
}
export function countsToCurrent(counts, config) {
    const adcVolts = (counts * config.adcReferenceVolts) / config.adcFullScaleCounts / config.pgaGain;
    return ((adcVolts / config.currentIsolationGain) / config.shuntOhms - config.currentOffset)
        * config.currentGainTrim;
}
export function calibrateFrame(frame, config, autoEstimateDcOffset = false) {
    const length = Math.min(frame.vCounts.length, frame.iCounts.length);
    const voltage = new Float64Array(length);
    const current = new Float64Array(length);
    let voltageMean = 0;
    let currentMean = 0;
    for (let index = 0; index < length; index += 1) {
        voltage[index] = countsToVoltage(frame.vCounts[index] ?? 0, config);
        current[index] = countsToCurrent(frame.iCounts[index] ?? 0, config);
        voltageMean += voltage[index] ?? 0;
        currentMean += current[index] ?? 0;
    }
    if (autoEstimateDcOffset && length > 0) {
        // Per-window mean removal is opt-in because it also removes any real DC component.
        voltageMean /= length;
        currentMean /= length;
        for (let index = 0; index < length; index += 1) {
            voltage[index] = (voltage[index] ?? 0) - voltageMean;
            current[index] = (current[index] ?? 0) - currentMean;
        }
    }
    return { raw: frame, voltage, current };
}
