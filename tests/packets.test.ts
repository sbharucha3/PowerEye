import { describe, expect, it } from "vitest";
import {
  aggregateSummary,
  decodeEventPacket,
  decodeSummaryPacket,
  downsampleEventWaveform,
  encodeEventPacket,
  encodeSummaryPacket,
} from "../src/packets.js";
import { createSyntheticScenario } from "../src/synth/generator.js";
import { PowerEyePipeline } from "../src/pipeline.js";

describe("packet codecs", () => {
  it("round-trips the 24-byte summary with specified quantization", () => {
    const packet = {
      timestampSeconds: 1_700_000_000,
      vrms: 120.126,
      irms: 4.5678,
      frequencyHz: 59.9996,
      pReal: 500.06,
      sApparent: 600.04,
      thdVPercent: 1.234,
      energyWh: 12.36,
      flags: 257,
      batteryPercent: 84,
      seq: 255,
    };
    const encoded = encodeSummaryPacket(packet);
    expect(encoded.length).toBe(24);
    const decoded = decodeSummaryPacket(encoded);
    expect(decoded.vrms).toBeCloseTo(packet.vrms, 2);
    expect(decoded.irms).toBeCloseTo(packet.irms, 3);
    expect(decoded.frequencyHz).toBeCloseTo(packet.frequencyHz, 3);
    expect(decoded.pReal).toBeCloseTo(packet.pReal, 1);
    expect(decoded.energyWh).toBeCloseTo(packet.energyWh, 1);
    expect(decoded.flags).toBe(packet.flags);
  });

  it("round-trips provisional event packets and waveform pairs", () => {
    const packet = {
      type: 1,
      nodeId: 9,
      timestampSeconds: 1234,
      flags: 5,
      peakVoltage: 140.23,
      peakCurrent: 12.345,
      waveform: Int16Array.of(-12, 34, 56, -78),
    };
    const decoded = decodeEventPacket(encodeEventPacket(packet));
    expect(decoded.nodeId).toBe(9);
    expect(decoded.peakVoltage).toBe(140.23);
    expect(decoded.waveform).toEqual(packet.waveform);
  });

  it("downsamples event waveform pairs to the wire limit while preserving endpoints", () => {
    const waveform = Int16Array.from({ length: 200 }, (_, index) => index);
    const reduced = downsampleEventWaveform(waveform);
    expect(reduced.length).toBe(128);
    expect(reduced[0]).toBe(0);
    expect(reduced[126]).toBe(198);
    expect(decodeEventPacket(encodeEventPacket({
      type: 1, nodeId: 1, timestampSeconds: 1, flags: 0,
      peakVoltage: 120, peakCurrent: 2, waveform,
    })).waveform?.length).toBe(128);
  });

  it("aggregates interval metrics, carries cumulative energy, and ORs flags", () => {
    const scenario = createSyntheticScenario("normal", { frameCount: 2 });
    const pipeline = new PowerEyePipeline();
    const first = pipeline.process({ ...scenario.frames[0]!, batteryPercent: 80 });
    const second = pipeline.process({ ...scenario.frames[1]!, batteryPercent: 78 });
    const summary = aggregateSummary([first, second], 12345, 7);
    expect(summary.vrms).toBeCloseTo((first.vrms + second.vrms) / 2, 5);
    expect(summary.energyWh).toBe(second.energyWh);
    expect(summary.batteryPercent).toBe(78);
    expect(aggregateSummary([{ ...first, flags: 4 }, second], 12345, 7).flags).toBe(first.flags | second.flags | 4);
    expect(() => aggregateSummary([], 12345, 7)).toThrow(RangeError);
  });
});
