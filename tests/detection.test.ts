import { describe, expect, it } from "vitest";
import { compareFilterOnOff } from "../src/compare.js";
import { PowerEyePipeline } from "../src/pipeline.js";
import { availableScenarios, createSyntheticScenario } from "../src/synth/generator.js";
import { Flag } from "../src/types.js";

describe("detection and optional filtering", () => {
  it("flags clipping, overcurrent, off-frequency operation, and distortion", () => {
    const clipping = createSyntheticScenario("clipping");
    expect(new PowerEyePipeline().process(clipping.frames[0]!).flags & Flag.Clipped).toBe(Flag.Clipped);
    const overcurrent = createSyntheticScenario("overcurrent");
    expect(new PowerEyePipeline().process(overcurrent.frames[0]!).flags & Flag.Current).toBe(Flag.Current);
    const frequency = createSyntheticScenario("frequency-step");
    expect(new PowerEyePipeline().process(frequency.frames[0]!).flags & Flag.Frequency).toBe(Flag.Frequency);
    const nonlinear = createSyntheticScenario("nonlinear", { thdPercent: 15 });
    expect(new PowerEyePipeline().process(nonlinear.frames[0]!).flags & Flag.Anomaly).toBe(Flag.Anomaly);
  });

  it("returns metric deltas between filtered and unfiltered analyses", () => {
    const frame = createSyntheticScenario("nonlinear").frames[0]!;
    const comparison = compareFilterOnOff(frame, 200);
    expect(comparison.withoutFilter).not.toBe(comparison.withFilter);
    expect(typeof comparison.deltas.vrms).toBe("number");
    expect(comparison.deltas.thdV).not.toBe(0);
    expect(typeof comparison.deltas["harmonicsV.1"]).toBe("number");
  });

  it("covers every seeded scenario without unrelated top-level flags", () => {
    const expectedFlags: Record<string, number> = {
      normal: 0,
      "reactive-lagging": 0,
      "reactive-leading": 0,
      nonlinear: Flag.Anomaly,
      sag: Flag.Voltage | Flag.VoltageSag | Flag.Anomaly,
      swell: Flag.Voltage | Flag.VoltageSwell | Flag.Anomaly,
      "sag-ramp": Flag.Voltage | Flag.VoltageSag | Flag.Anomaly,
      "swell-ramp": Flag.Voltage | Flag.VoltageSwell | Flag.Anomaly,
      overcurrent: Flag.Current,
      "frequency-step": Flag.Frequency,
      "frequency-wander": 0,
      noise: 0,
      "dc-offset": 0,
      "gain-error": 0,
      spike: Flag.Anomaly,
      transient: Flag.Anomaly,
      clipping: Flag.Voltage | Flag.VoltageSwell | Flag.Anomaly | Flag.Clipped,
      "dropped-frame": Flag.DataGap,
      "duplicate-frame": 0,
      "out-of-order-frame": 0,
    };
    for (const name of availableScenarios()) {
      const scenario = createSyntheticScenario(name);
      const pipeline = new PowerEyePipeline();
      const results = scenario.frames.map((frame) => pipeline.process(frame));
      const result = results[results.length - 1]!;
      expect(result.flags, name).toBe(expectedFlags[name]);
      if (name === "duplicate-frame") {
        expect(results.some((entry) => entry.sequenceStatus === "duplicate")).toBe(true);
      }
      if (name === "out-of-order-frame") {
        expect(results.some((entry) => entry.sequenceStatus === "out_of_order")).toBe(true);
      }
    }
  });

  it("debounces set/clear transitions and resets stream state", () => {
    const high = createSyntheticScenario("overcurrent").frames[0]!;
    const normal = createSyntheticScenario("normal").frames[0]!;
    const pipeline = new PowerEyePipeline({ debounceWindows: 2, debounceClearWindows: 2 });
    const window = (frame: typeof high, seq: number): typeof high => ({
      ...frame,
      seq,
      tStartMs: seq * 1000,
    });
    expect(pipeline.process(window(normal, 0)).flags & Flag.Current).toBe(0);
    expect(pipeline.process(window(high, 1)).flags & Flag.Current).toBe(0);
    expect(pipeline.process(window(high, 2)).flags & Flag.Current).toBe(Flag.Current);
    expect(pipeline.process(window(normal, 3)).flags & Flag.Current).toBe(Flag.Current);
    expect(pipeline.process(window(normal, 4)).flags & Flag.Current).toBe(0);
    pipeline.reset();
    expect(pipeline.process(window(high, 0)).flags & Flag.Current).toBe(0);
  });
});
