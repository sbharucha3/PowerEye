function fitAtFrequency(signal, sampleRateHz, frequencyHz) {
    let ss = 0;
    let cc = 0;
    let sc = 0;
    let sy = 0;
    let cy = 0;
    let sumSine = 0;
    let sumCosine = 0;
    let sumY = 0;
    const count = signal.length;
    for (let index = 0; index < count; index += 1) {
        const angle = (2 * Math.PI * frequencyHz * index) / sampleRateHz;
        const sine = Math.sin(angle);
        const cosine = Math.cos(angle);
        const value = signal[index] ?? 0;
        ss += sine * sine;
        cc += cosine * cosine;
        sc += sine * cosine;
        sy += sine * value;
        cy += cosine * value;
        sumSine += sine;
        sumCosine += cosine;
        sumY += value;
    }
    const meanY = count === 0 ? 0 : sumY / count;
    // Eliminate the offset term from the normal equations before solving for sine/cosine coefficients.
    const centeredSS = ss - (sumSine * sumSine) / count;
    const centeredCC = cc - (sumCosine * sumCosine) / count;
    const centeredSC = sc - (sumSine * sumCosine) / count;
    const determinant = centeredSS * centeredCC - centeredSC * centeredSC;
    const centeredSy = sy - sumSine * meanY;
    const centeredCy = cy - sumCosine * meanY;
    const sineCoefficient = Math.abs(determinant) < 1e-18
        ? 0
        : (centeredSy * centeredCC - centeredCy * centeredSC) / determinant;
    const cosineCoefficient = Math.abs(determinant) < 1e-18
        ? 0
        : (centeredCy * centeredSS - centeredSy * centeredSC) / determinant;
    const offset = meanY;
    let error = 0;
    for (let index = 0; index < count; index += 1) {
        const angle = (2 * Math.PI * frequencyHz * index) / sampleRateHz;
        const residual = (signal[index] ?? 0) - (offset + sineCoefficient * Math.sin(angle) + cosineCoefficient * Math.cos(angle));
        error += residual * residual;
    }
    return {
        error: count === 0 ? 0 : error / count,
        amplitude: Math.hypot(sineCoefficient, cosineCoefficient),
        phase: Math.atan2(cosineCoefficient, sineCoefficient),
        offset,
    };
}
export function fitSineFrequency(signal, sampleRateHz, initialFrequencyHz, searchRangeHz = 2, searchSteps = 200) {
    if (signal.length < 3 || initialFrequencyHz === null || !Number.isFinite(initialFrequencyHz)) {
        return { frequencyHz: null, amplitude: 0, phaseRadians: 0, offset: 0, residualRms: 0 };
    }
    const steps = Math.max(3, Math.floor(searchSteps));
    const low = Math.max(Number.EPSILON, initialFrequencyHz - searchRangeHz);
    const high = Math.min(sampleRateHz / 2, initialFrequencyHz + searchRangeHz);
    const step = (high - low) / steps;
    let bestIndex = 0;
    let bestFit = fitAtFrequency(signal, sampleRateHz, low);
    for (let index = 1; index <= steps; index += 1) {
        const fit = fitAtFrequency(signal, sampleRateHz, low + step * index);
        if (fit.error < bestFit.error) {
            bestIndex = index;
            bestFit = fit;
        }
    }
    let refinedFrequency = low + step * bestIndex;
    // Parabolic interpolation around the best grid point removes most scan quantization cheaply.
    if (bestIndex > 0 && bestIndex < steps) {
        const left = fitAtFrequency(signal, sampleRateHz, refinedFrequency - step).error;
        const right = fitAtFrequency(signal, sampleRateHz, refinedFrequency + step).error;
        const denominator = left - 2 * bestFit.error + right;
        if (Math.abs(denominator) > 1e-18) {
            const offset = Math.max(-1, Math.min(1, 0.5 * (left - right) / denominator));
            refinedFrequency += offset * step;
            bestFit = fitAtFrequency(signal, sampleRateHz, refinedFrequency);
        }
    }
    return {
        frequencyHz: refinedFrequency,
        amplitude: bestFit.amplitude,
        phaseRadians: bestFit.phase,
        offset: bestFit.offset,
        residualRms: Math.sqrt(bestFit.error),
    };
}
