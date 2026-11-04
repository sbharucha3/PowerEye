export function butterworthLowPassCoefficients(sampleRateHz, cutoffHz) {
    if (!(sampleRateHz > 0) || !(cutoffHz > 0) || cutoffHz >= sampleRateHz / 2) {
        throw new RangeError("Low-pass cutoff must be between 0 and the Nyquist frequency.");
    }
    const omega = (2 * Math.PI * cutoffHz) / sampleRateHz;
    const cosine = Math.cos(omega);
    // Q = 1/sqrt(2) gives the maximally-flat second-order Butterworth response.
    const alpha = Math.sin(omega) / Math.SQRT2;
    const a0 = 1 + alpha;
    return {
        b0: ((1 - cosine) / 2) / a0,
        b1: (1 - cosine) / a0,
        b2: ((1 - cosine) / 2) / a0,
        a1: (-2 * cosine) / a0,
        a2: (1 - alpha) / a0,
    };
}
export function applyBiquad(signal, coefficients) {
    // The direct-form recurrence uses scalar state, avoiding allocations inside the sample loop.
    const output = new Float64Array(signal.length);
    let x1 = 0;
    let x2 = 0;
    let y1 = 0;
    let y2 = 0;
    for (let index = 0; index < signal.length; index += 1) {
        const x0 = signal[index] ?? 0;
        const y0 = coefficients.b0 * x0 + coefficients.b1 * x1 + coefficients.b2 * x2
            - coefficients.a1 * y1 - coefficients.a2 * y2;
        output[index] = y0;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
    }
    return output;
}
