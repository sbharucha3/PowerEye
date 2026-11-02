import { describe, expect, it } from "vitest";
import { DEFAULT_PIPELINE_CONFIG } from "../src/config.js";
import { findCycleRegion } from "../src/cycles.js";
import { calibrateFrame } from "../src/calibration.js";
import { calculatePower, EnergyAccumulator } from "../src/power.js";
import { createSyntheticScenario } from "../src/synth/generator.js";
import { PowerEyePipeline } from "../src/pipeline.js";
import { Flag } from "../src/types.js";
import type { RawFrame } from "../src/types.js";

function frameFromSignals(voltage: number[], current = voltage, sampleRateHz = 4000): RawFrame {
  const config = DEFAULT_PIPELINE_CONFIG.device;
  const toCount = (value: number, channel: "voltage" | "current"): number => {
    const voltsAtAdc = channel === "voltage"
      ? (value / config.voltageDividerRatio) * config.voltageIsolationGain
      : value * config.shuntOhms * config.currentIsolationGain;
    return Math.round((voltsAtAdc * config.adcFullScaleCounts) / config.adcReferenceVolts);
  };
  return {
    nodeId: 7,
    seq: 1,
    tStartMs: 0,
    sampleRateHz,
    vCounts: Int32Array.from(voltage, (value) => toCount(value, "voltage")),
    iCounts: Int32Array.from(current, (value) => toCount(value, "current")),
  };
}

describe("calibration and power", () => {
  it("recovers physical units and computes power metrics", () => {
    const calibrated = calibrateFrame(frameFromSignals([120, -120], [5, -5]), DEFAULT_PIPELINE_CONFIG.device);
    expect(calibrated.voltage[0]).toBeCloseTo(120, 1);
    expect(calibrated.current[0]).toBeCloseTo(5, 1);
    const power = calculatePower(Float64Array.of(120, -120), Float64Array.of(5, -5));
    expect(power.vrms).toBe(120);
    expect(power.irms).toBe(5);
    expect(power.pReal).toBe(600);
    expect(power.powerFactor).toBe(1);
  });

  it("accumulates watt-hours and ignores invalid intervals", () => {
    const energy = new EnergyAccumulator();
    expect(energy.add(3600, 1)).toBe(1);
    expect(energy.add(100, 0)).toBe(1);
  });
});

describe("cycle alignment and pipeline", () => {
  it("detects an integer-cycle region", () => {
    const signal = Float64Array.from({ length: 4000 }, (_, index) => Math.sin(2 * Math.PI * 60 * index / 4000) * 120);
    const region = findCycleRegion(signal, 4000, 0.1);
    expect(region.cycleCount).toBeGreaterThanOrEqual(58);
    expect(region.frequencyHz).toBeCloseTo(60, 2);
  });

  it("measures nominal synthetic data within project targets", () => {
    const scenario = createSyntheticScenario("normal");
    const result = new PowerEyePipeline().process(scenario.frames[0]!);
    expect(result.validity).toBe("ok");
    expect(Math.abs(result.vrms - scenario.groundTruth.vrms) / scenario.groundTruth.vrms).toBeLessThan(0.005);
    expect(Math.abs(result.irms - scenario.groundTruth.irms) / scenario.groundTruth.irms).toBeLessThan(0.03);
    expect(Math.abs((result.freqHz ?? 0) - 60)).toBeLessThan(0.01);
    expect(result.pReal).toBeCloseTo(scenario.groundTruth.pReal, 0);
    expect(Number.isFinite(result.energyWh)).toBe(true);
  });

  it("meets frequency and THD targets at 40 dB SNR", () => {
    const noisy = createSyntheticScenario("noise", { snrDb: 40 });
    const noisyResult = new PowerEyePipeline().process(noisy.frames[0]!);
    expect(Math.abs((noisyResult.freqHz ?? 0) - 60)).toBeLessThan(0.01);
    const distorted = createSyntheticScenario("nonlinear", { thdPercent: 12 });
    const distortedResult = new PowerEyePipeline().process(distorted.frames[0]!);
    expect(Math.abs((distortedResult.thdV ?? 0) - distorted.groundTruth.thdVPercent)).toBeLessThan(0.5);
    expect(Math.abs((distortedResult.thdI ?? 0) - distorted.groundTruth.thdIPercent)).toBeLessThan(0.5);
  });

  it("supports shorter frames and reports null THD for a zero-current fundamental", () => {
    const short = createSyntheticScenario("normal", { samplesPerFrame: 1000 }).frames[0]!;
    const shortResult = new PowerEyePipeline().process(short);
    expect(shortResult.validity).toBe("ok");
    expect(Math.abs(shortResult.vrms - 120)).toBeLessThan(0.6);
    const voltage = Array.from({ length: 4000 }, (_, index) => 120 * Math.SQRT2 * Math.sin(2 * Math.PI * 60 * index / 4000));
    const noLoad = new PowerEyePipeline().process(frameFromSignals(voltage, Array(4000).fill(0)));
    expect(noLoad.irms).toBeCloseTo(0, 5);
    expect(noLoad.thdI).toBeNull();
  });

  it("reports sequence gaps and avoids integrating a missing interval", () => {
    const scenario = createSyntheticScenario("normal", { frameCount: 3 });
    const pipeline = new PowerEyePipeline();
    const first = pipeline.process(scenario.frames[0]!);
    const gapFrame = { ...scenario.frames[1]!, seq: scenario.frames[1]!.seq + 1 };
    const result = pipeline.process(gapFrame);
    expect(result.sequenceStatus).toBe("gap");
    expect(result.gapFrames).toBe(1);
    expect(result.flags & Flag.DataGap).toBe(Flag.DataGap);
    expect(result.energyWh - first.energyWh).toBeGreaterThan(0);
    expect(pipeline.process(gapFrame).energyWh).toBe(result.energyWh);
  });

  it("supports disabling optional stages and passes battery percentage through", () => {
    const frame = {
      ...createSyntheticScenario("nonlinear").frames[0]!,
      batteryPercent: 73,
    };
    const result = new PowerEyePipeline({
      frequencyFitEnabled: false,
      harmonics: 0,
      detectionEnabled: false,
    }).process(frame);
    expect(result.freqFit).toBeNull();
    expect(result.harmonicsV.length).toBe(0);
    expect(result.flags).toBe(0);
    expect(result.batteryPercent).toBe(73);
  });

  it("returns finite, explicit validity states for empty, silent, and short frames", () => {
    const pipeline = new PowerEyePipeline();
    const base = frameFromSignals([]);
    const emptyResult = pipeline.process(base);
    expect(emptyResult.validity).toBe("no_signal");
    const silent = { ...frameFromSignals(Array(4000).fill(0)), seq: 2, tStartMs: 0 };
    expect(pipeline.process(silent).validity).toBe("no_signal");
    const singleCycle = Array.from({ length: 67 }, (_, index) => 120 * Math.sin(2 * Math.PI * 60 * index / 4000));
    const short = { ...frameFromSignals(singleCycle), seq: 3 };
    expect(new PowerEyePipeline().process(short).validity).toBe("too_few_cycles");
    expect([
      emptyResult.vrms, emptyResult.irms, emptyResult.pReal, emptyResult.sApparent,
      emptyResult.qReactive, emptyResult.powerFactor, emptyResult.energyWh,
    ].every(Number.isFinite)).toBe(true);
  });

  it("keeps calculations finite for very large calibration offsets", () => {
    const source = createSyntheticScenario("normal").frames[0]!;
    const result = new PowerEyePipeline({
      device: { voltageOffset: 1e12, currentOffset: -1e12 },
    }).process(source);
    expect(Number.isFinite(result.vrms)).toBe(true);
    expect(Number.isFinite(result.irms)).toBe(true);
    expect(Number.isFinite(result.pReal)).toBe(true);
  });
});
