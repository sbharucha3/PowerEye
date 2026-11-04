import type { RawFrame } from "../types.js";
export interface RawFrameSource {
    next(): RawFrame | null;
}
/** CSV replay uses one sample per row; frameId optionally distinguishes repeated frame metadata. */
export declare class CsvReplaySource implements RawFrameSource {
    private readonly frames;
    private cursor;
    constructor(csvText: string);
    next(): RawFrame | null;
}
export declare class ArrayFrameSource implements RawFrameSource {
    private readonly frames;
    private cursor;
    constructor(frames: readonly RawFrame[]);
    next(): RawFrame | null;
}
