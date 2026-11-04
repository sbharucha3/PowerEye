export interface PowerMetrics {
    vrms: number;
    irms: number;
    pReal: number;
    sApparent: number;
    qReactive: number;
    powerFactor: number;
    vPeak: number;
    iPeak: number;
    crestV: number;
    crestI: number;
}
export declare function calculatePower(voltage: Float64Array, current: Float64Array, startIndex?: number, endIndex?: number): PowerMetrics;
export declare class EnergyAccumulator {
    private totalWh;
    add(realPowerWatts: number, durationSeconds: number): number;
    value(): number;
    reset(): void;
}
