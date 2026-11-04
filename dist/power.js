export function calculatePower(voltage, current, startIndex = 0, endIndex = Math.min(voltage.length, current.length)) {
    // One shared region ensures RMS and real power describe the same integer-cycle interval.
    const start = Math.max(0, Math.ceil(startIndex));
    const end = Math.min(voltage.length, current.length, Math.floor(endIndex));
    let sumV2 = 0;
    let sumI2 = 0;
    let sumVI = 0;
    let vPeak = 0;
    let iPeak = 0;
    const count = Math.max(0, end - start);
    for (let index = start; index < end; index += 1) {
        const v = voltage[index] ?? 0;
        const i = current[index] ?? 0;
        sumV2 += v * v;
        sumI2 += i * i;
        sumVI += v * i;
        vPeak = Math.max(vPeak, Math.abs(v));
        iPeak = Math.max(iPeak, Math.abs(i));
    }
    const vrms = count === 0 ? 0 : Math.sqrt(sumV2 / count);
    const irms = count === 0 ? 0 : Math.sqrt(sumI2 / count);
    const pReal = count === 0 ? 0 : sumVI / count;
    const sApparent = vrms * irms;
    // At zero apparent power PF has no physical meaning; return a finite neutral value.
    const powerFactor = sApparent <= Number.EPSILON
        ? 0
        : Math.max(-1, Math.min(1, pReal / sApparent));
    return {
        vrms,
        irms,
        pReal,
        sApparent,
        qReactive: Math.sqrt(Math.max(sApparent * sApparent - pReal * pReal, 0)),
        powerFactor,
        vPeak,
        iPeak,
        crestV: vrms <= Number.EPSILON ? 0 : vPeak / vrms,
        crestI: irms <= Number.EPSILON ? 0 : iPeak / irms,
    };
}
export class EnergyAccumulator {
    totalWh = 0;
    add(realPowerWatts, durationSeconds) {
        // Integrate only the caller-provided measured interval; missing intervals are never inferred here.
        if (Number.isFinite(realPowerWatts) && Number.isFinite(durationSeconds) && durationSeconds > 0) {
            this.totalWh += (realPowerWatts * durationSeconds) / 3600;
        }
        return this.totalWh;
    }
    value() {
        return this.totalWh;
    }
    reset() {
        this.totalWh = 0;
    }
}
