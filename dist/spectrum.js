export function analyzeHarmonics(signal, sampleRateHz, fundamentalHz, requestedHarmonics, startIndex = 0, endIndex = signal.length) {
    if (!Number.isFinite(sampleRateHz) || sampleRateHz <= 0
        || !Number.isFinite(fundamentalHz) || fundamentalHz < 0
        || !Number.isInteger(requestedHarmonics) || requestedHarmonics < 0) {
        throw new RangeError("Spectrum analysis requires a positive sample rate and valid frequency/harmonic count.");
    }
    if (fundamentalHz === 0 || requestedHarmonics === 0) {
        return { amplitude: new Float64Array(0), phase: new Float64Array(0) };
    }
    const count = Math.max(0, Math.min(signal.length, Math.floor(endIndex)) - Math.max(0, Math.ceil(startIndex)));
    const maxHarmonics = Math.max(0, Math.min(Math.floor(requestedHarmonics), Math.floor((sampleRateHz / 2 - Number.EPSILON) / fundamentalHz)));
    const amplitude = new Float64Array(maxHarmonics);
    const phase = new Float64Array(maxHarmonics);
    if (count === 0 || !(fundamentalHz > 0))
        return { amplitude, phase };
    // Evaluate the known fundamental and its multiples directly instead of binning an FFT,
    // which avoids leakage when the measured frequency is not an integer FFT-bin frequency.
    const start = Math.max(0, Math.ceil(startIndex));
    for (let harmonic = 1; harmonic <= maxHarmonics; harmonic += 1) {
        let sineSum = 0;
        let cosineSum = 0;
        for (let offset = 0; offset < count; offset += 1) {
            const angle = (2 * Math.PI * harmonic * fundamentalHz * offset) / sampleRateHz;
            const value = signal[start + offset] ?? 0;
            sineSum += value * Math.sin(angle);
            cosineSum += value * Math.cos(angle);
        }
        const sineCoefficient = (2 * sineSum) / count;
        const cosineCoefficient = (2 * cosineSum) / count;
        amplitude[harmonic - 1] = Math.hypot(sineCoefficient, cosineCoefficient);
        phase[harmonic - 1] = Math.atan2(cosineCoefficient, sineCoefficient);
    }
    return { amplitude, phase };
}
export function calculateThd(harmonics, minimumFundamental) {
    if (!Number.isFinite(minimumFundamental) || minimumFundamental < 0) {
        throw new RangeError("Minimum THD fundamental must be a finite non-negative amplitude.");
    }
    const fundamental = harmonics[0] ?? 0;
    if (harmonics.length === 0 || !Number.isFinite(fundamental) || fundamental <= minimumFundamental)
        return null;
    let squaredHarmonics = 0;
    for (let index = 1; index < harmonics.length; index += 1) {
        squaredHarmonics += (harmonics[index] ?? 0) ** 2;
    }
    return (Math.sqrt(squaredHarmonics) / fundamental) * 100;
}
