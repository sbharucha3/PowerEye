export function findRisingZeroCrossings(signal, hysteresis = 0) {
    const found = [];
    let armed = false;
    for (let index = 0; index < signal.length; index += 1) {
        const value = signal[index] ?? 0;
        // Require a negative excursion before re-arming, so noisy zero chatter is not counted as cycles.
        if (value <= -hysteresis)
            armed = true;
        if (armed && index > 0) {
            const previous = signal[index - 1] ?? 0;
            if (previous <= 0 && value > 0) {
                const denominator = value - previous;
                const fraction = denominator === 0 ? 0 : -previous / denominator;
                found.push(index - 1 + fraction);
                armed = false;
            }
        }
    }
    return Float64Array.from(found);
}
export function findCycleRegion(signal, sampleRateHz, hysteresis = 0) {
    const crossings = findRisingZeroCrossings(signal, hysteresis);
    const cycleCount = Math.max(0, crossings.length - 1);
    // Averaging periods is less sensitive to a single noisy crossing than using one cycle.
    let periodSum = 0;
    for (let index = 1; index < crossings.length; index += 1) {
        periodSum += (crossings[index] ?? 0) - (crossings[index - 1] ?? 0);
    }
    const meanPeriod = cycleCount > 0 ? periodSum / cycleCount : 0;
    const startPosition = crossings[0] ?? 0;
    const endPosition = crossings[crossings.length - 1] ?? 0;
    return {
        crossings,
        startPosition,
        endPosition,
        startIndex: Math.ceil(startPosition),
        endIndex: Math.floor(endPosition) + 1,
        cycleCount,
        frequencyHz: meanPeriod > 0 ? sampleRateHz / meanPeriod : null,
    };
}
