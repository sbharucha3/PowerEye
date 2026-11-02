import { availableScenarios, createSyntheticScenario } from "../src/synth/generator.js";
import { PowerEyePipeline } from "../src/pipeline.js";

console.log("scenario              V true/meas/error%      I true/meas/error%      P true/meas/error%      f true/meas/error%   THD V true/meas   THD I true/meas   sequence / flags");
for (const name of availableScenarios()) {
  const scenario = createSyntheticScenario(name);
  const pipeline = new PowerEyePipeline();
  const results = scenario.frames.map((frame) => pipeline.process(frame));
  const result = results[results.length - 1];
  if (!result) continue;
  const truth = scenario.groundTruth;
  const fmt = (value: number | null): string => value === null ? "n/a" : value.toFixed(2);
  const error = (measured: number | null, actual: number): string => {
    if (measured === null || actual === 0) return "n/a";
    const percent = ((measured - actual) / actual) * 100;
    return `${(Math.abs(percent) < 0.005 ? 0 : percent).toFixed(2)}%`;
  };
  console.log(
    `${name.padEnd(21)} ${fmt(truth.vrms).padStart(6)}/${fmt(result.vrms).padStart(6)}/${error(result.vrms, truth.vrms).padStart(7)}`
    + ` ${fmt(truth.irms).padStart(6)}/${fmt(result.irms).padStart(6)}/${error(result.irms, truth.irms).padStart(7)}`
    + ` ${fmt(truth.pReal).padStart(7)}/${fmt(result.pReal).padStart(7)}/${error(result.pReal, truth.pReal).padStart(7)}`
    + ` ${fmt(truth.frequencyHz).padStart(6)}/${fmt(result.freqHz).padStart(6)}/${error(result.freqHz, truth.frequencyHz).padStart(7)}`
    + ` ${fmt(truth.thdVPercent).padStart(5)}/${fmt(result.thdV).padStart(5)}`
    + ` ${fmt(truth.thdIPercent).padStart(5)}/${fmt(result.thdI).padStart(5)}`
    + ` ${results.map((entry) => entry.sequenceStatus).join(",")} / ${result.flagReasons.join(",") || "none"}`,
  );
}
