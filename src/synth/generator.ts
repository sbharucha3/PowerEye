import {
  DEFAULT_PIPELINE_CONFIG,
  makePipelineConfig,
  type PipelineConfig,
  type PipelineConfigOverrides,
} from "../config.js";
import type { RawFrame, SyntheticGroundTruth, SyntheticScenario } from "../types.js";

export type ScenarioName =
  | "normal" | "reactive-lagging" | "reactive-leading" | "nonlinear"
  | "sag" | "swell" | "sag-ramp" | "swell-ramp" | "overcurrent" | "frequency-step" | "frequency-wander"
  | "noise" | "dc-offset" | "gain-error" | "spike" | "transient" | "clipping"
  | "dropped-frame" | "duplicate-frame" | "out-of-order-frame";

export interface SyntheticOptions {
  seed?: number;
  sampleRateHz?: number;
  samplesPerFrame?: number;
  frameCount?: number;
  nodeId?: number;
  sequenceStart?: number;
  snrDb?: number;
  currentAmps?: number;
  voltageRms?: number;
  frequencyHz?: number;
  thdPercent?: number;
  config?: PipelineConfigOverrides;
}

function seededRandom(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let value = state;
    value = Math.imul(value ^ (value >>> 15), value | 1);
    value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
    return ((value ^ (value >>> 14)) >>> 0) / 0x100000000;
  };
}

function gaussian(random: () => number): number {
  const first = Math.max(random(), 1e-12);
  const second = random();
  return Math.sqrt(-2 * Math.log(first)) * Math.cos(2 * Math.PI * second);
}

function toCounts(value: number, channel: "voltage" | "current", config: PipelineConfig): number {
  const device = config.device;
  const trim = channel === "voltage" ? device.voltageGainTrim : device.currentGainTrim;
  const offset = channel === "voltage" ? device.voltageOffset : device.currentOffset;
  const isolation = channel === "voltage" ? device.voltageIsolationGain : device.currentIsolationGain;
  // Generation runs calibration in reverse so tests exercise the same counts-to-units path as hardware.
  const voltsAtAdc = channel === "voltage"
    ? (((value / trim + offset) / device.voltageDividerRatio) * isolation)
    : ((value / trim + offset) * device.shuntOhms * isolation);
  return Math.round((voltsAtAdc * device.pgaGain * device.adcFullScaleCounts) / device.adcReferenceVolts);
}

function clampCounts(count: number, fullScale: number): number {
  return Math.max(-fullScale, Math.min(fullScale - 1, count));
}

export function createSyntheticScenario(
  name: ScenarioName,
  options: SyntheticOptions = {},
): SyntheticScenario {
  const baseConfig = makePipelineConfig(options.config);
  const config = makePipelineConfig({
    ...baseConfig,
  });
  const sampleRateHz = options.sampleRateHz ?? config.device.sampleRateHz;
  const samplesPerFrame = options.samplesPerFrame ?? sampleRateHz;
  const sequenceScenario = name === "dropped-frame" || name === "duplicate-frame" || name === "out-of-order-frame";
  const frameCount = Math.max(1, Math.floor(options.frameCount ?? (sequenceScenario ? 3 : 1)));
  const random = seededRandom(options.seed ?? 1);
  const voltageRms = options.voltageRms ?? config.device.nominalVoltageRms;
  const currentRms = options.currentAmps ?? 5;
  let frequencyHz = options.frequencyHz ?? config.device.nominalFrequencyHz;
  if (name === "frequency-step") frequencyHz = options.frequencyHz ?? config.device.nominalFrequencyHz - 0.7;
  const sagTarget = voltageRms * 0.7;
  const swellTarget = voltageRms * 1.21;
  let phase = 0;
  let voltagePeak = voltageRms * Math.SQRT2;
  let currentPeak = currentRms * Math.SQRT2;
  let currentPhase = name === "reactive-leading" ? Math.PI / 4 : name === "reactive-lagging" ? -Math.PI / 4 : 0;
  const thdRatio = (options.thdPercent ?? 12) / 100;
  let noiseSigma = 0;
  const overcurrentAmps = Math.max(config.currentLimitAmps, config.device.defaultCurrentLimitAmps) + 5;
  if (name === "overcurrent") currentPeak = overcurrentAmps * Math.SQRT2;
  if (name === "noise") {
    const snrDb = options.snrDb ?? 40;
    noiseSigma = voltagePeak / Math.sqrt(2) / 10 ** (snrDb / 20);
  }

  const frames: RawFrame[] = [];
  let sequence = options.sequenceStart ?? 0;
  for (let frameIndex = 0; frameIndex < frameCount; frameIndex += 1) {
    const vCounts = new Int32Array(samplesPerFrame);
    const iCounts = new Int32Array(samplesPerFrame);
    for (let sampleIndex = 0; sampleIndex < samplesPerFrame; sampleIndex += 1) {
      const absoluteSample = frameIndex * samplesPerFrame + sampleIndex;
      const time = absoluteSample / sampleRateHz;
      if (name === "frequency-wander") {
        frequencyHz = config.device.nominalFrequencyHz + 0.2 * Math.sin(2 * Math.PI * 0.05 * time);
      }
      if (name === "sag") voltagePeak = (time < 0.5 ? voltageRms : sagTarget) * Math.SQRT2;
      if (name === "swell") voltagePeak = (time < 0.5 ? voltageRms : swellTarget) * Math.SQRT2;
      if (name === "sag-ramp" || name === "swell-ramp") {
        const target = name === "sag-ramp" ? sagTarget : swellTarget;
        const fraction = Math.max(0, Math.min(1, (time - 0.2) / 0.6));
        voltagePeak = (voltageRms + (target - voltageRms) * fraction) * Math.SQRT2;
      }
      const transientScale = name === "transient" && Math.abs(time - 0.5) < 0.01 ? 1.2 : 1;
      phase += (2 * Math.PI * frequencyHz) / sampleRateHz;
      let v = voltagePeak * transientScale * (name === "clipping" ? 5 : 1) * Math.sin(phase);
      let i = currentPeak * Math.sin(phase + currentPhase);
      if (name === "nonlinear") {
        v += voltagePeak * thdRatio * Math.sin(3 * phase);
        i += currentPeak * thdRatio * (Math.sin(3 * phase) + Math.sin(5 * phase) + Math.sin(7 * phase)) / Math.sqrt(3);
      }
      if (name === "dc-offset") {
        v += 5;
        i += 0.25;
      }
      if (name === "noise") {
        v += gaussian(random) * noiseSigma;
        i += gaussian(random) * noiseSigma * 0.05;
      }
      if (name === "spike" && absoluteSample === Math.floor(samplesPerFrame / 2)) v += voltagePeak * 1.5;
      let vCount = toCounts(v, "voltage", config);
      let iCount = toCounts(i, "current", config);
      if (name === "gain-error") {
        vCount = Math.round(vCount * 1.02);
        iCount = Math.round(iCount * 0.98);
      }
      vCounts[sampleIndex] = clampCounts(vCount, config.device.adcFullScaleCounts);
      iCounts[sampleIndex] = clampCounts(iCount, config.device.adcFullScaleCounts);
    }
    const seqOffset = name === "dropped-frame" && frameIndex >= 1 ? 2 : 1;
    if (name === "out-of-order-frame" && frameIndex === 2) sequence -= 2;
    const frame: RawFrame = {
      nodeId: options.nodeId ?? 1,
      seq: sequence & 0xffff,
      tStartMs: (frameIndex * samplesPerFrame * 1000) / sampleRateHz,
      sampleRateHz,
      vCounts,
      iCounts,
    };
    frames.push(frame);
    if (name === "duplicate-frame" && frameIndex === 0) {
      frames.push({ ...frame, vCounts: frame.vCounts.slice(), iCounts: frame.iCounts.slice() });
    }
    sequence = (sequence + seqOffset) & 0xffff;
  }
  const reactiveAngle = name === "reactive-leading" ? Math.PI / 4 : name === "reactive-lagging" ? -Math.PI / 4 : 0;
  const rampMeanSquare = (nominal: number, target: number): number =>
    0.2 * nominal ** 2 + 0.6 * (nominal ** 2 + nominal * target + target ** 2) / 3 + 0.2 * target ** 2;
  let truthVoltage = voltageRms;
  if (name === "sag") truthVoltage = Math.sqrt((voltageRms ** 2 + sagTarget ** 2) / 2);
  else if (name === "sag-ramp") truthVoltage = Math.sqrt(rampMeanSquare(voltageRms, sagTarget));
  else if (name === "swell") truthVoltage = Math.sqrt((voltageRms ** 2 + swellTarget ** 2) / 2);
  else if (name === "swell-ramp") truthVoltage = Math.sqrt(rampMeanSquare(voltageRms, swellTarget));
  else if (name === "nonlinear") truthVoltage = voltageRms * Math.sqrt(1 + thdRatio ** 2);
  else if (name === "clipping") truthVoltage = voltageRms * 5;
  else if (name === "transient") truthVoltage = voltageRms * Math.sqrt(0.98 + 0.02 * 1.2 ** 2);
  const truthCurrent = name === "overcurrent" ? overcurrentAmps : currentRms;
  let truthPower = truthVoltage * truthCurrent * Math.cos(reactiveAngle);
  if (name === "sag" || name === "sag-ramp") {
    truthPower = 0.5 * (voltageRms + sagTarget) * truthCurrent * Math.cos(reactiveAngle);
  } else if (name === "swell" || name === "swell-ramp") {
    truthPower = 0.5 * (voltageRms + swellTarget) * truthCurrent * Math.cos(reactiveAngle);
  } else if (name === "nonlinear") {
    truthPower = voltageRms * truthCurrent * (1 + thdRatio ** 2 / Math.sqrt(3)) * Math.cos(reactiveAngle);
  } else if (name === "transient") {
    truthPower = voltageRms * truthCurrent * (0.98 + 0.02 * 1.2) * Math.cos(reactiveAngle);
  }
  const groundTruth: SyntheticGroundTruth = {
    vrms: truthVoltage,
    irms: name === "nonlinear" ? truthCurrent * Math.sqrt(1 + thdRatio ** 2) : truthCurrent,
    pReal: truthPower,
    frequencyHz,
    thdVPercent: name === "nonlinear" ? (options.thdPercent ?? 12) : 0,
    thdIPercent: name === "nonlinear" ? options.thdPercent ?? 12 : 0,
  };
  return { name, frames, groundTruth };
}

export function availableScenarios(): readonly ScenarioName[] {
  return [
    "normal", "reactive-lagging", "reactive-leading", "nonlinear", "sag", "swell", "sag-ramp", "swell-ramp",
    "overcurrent", "frequency-step", "frequency-wander", "noise", "dc-offset",
    "gain-error", "spike", "transient", "clipping", "dropped-frame",
    "duplicate-frame", "out-of-order-frame",
  ];
}

export const DEFAULT_SYNTHETIC_SAMPLE_RATE = DEFAULT_PIPELINE_CONFIG.device.sampleRateHz;
