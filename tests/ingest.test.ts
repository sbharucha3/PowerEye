import { describe, expect, it } from "vitest";
import { CsvReplaySource } from "../src/ingest/index.js";

describe("CSV replay source", () => {
  it("groups rows into ordered frames", () => {
    const csv = [
      "nodeId,seq,tStartMs,sampleRateHz,vCounts,iCounts",
      "1,4,0,4000,10,-10",
      "1,4,0,4000,11,-11",
      "1,5,1,4000,12,-12",
    ].join("\n");
    const source = new CsvReplaySource(csv);
    expect(source.next()?.vCounts).toEqual(Int32Array.of(10, 11));
    expect(source.next()?.seq).toBe(5);
    expect(source.next()).toBeNull();
  });

  it("uses frameId to preserve duplicate-sequence frames and rejects invalid metadata", () => {
    const csv = [
      "nodeId,seq,tStartMs,sampleRateHz,vCounts,iCounts,frameId",
      "1,4,0,4000,10,-10,a",
      "1,4,0,4000,11,-11,b",
    ].join("\n");
    const source = new CsvReplaySource(csv);
    expect(source.next()?.vCounts).toEqual(Int32Array.of(10));
    expect(source.next()?.vCounts).toEqual(Int32Array.of(11));
    expect(() => new CsvReplaySource([
      "nodeId,seq,tStartMs,sampleRateHz,vCounts,iCounts",
      "1,65536,0,4000,10,-10",
    ].join("\n"))).toThrow(/Invalid frame metadata/);
  });
});
