/** CSV replay uses one sample per row; frameId optionally distinguishes repeated frame metadata. */
export class CsvReplaySource {
    frames;
    cursor = 0;
    constructor(csvText) {
        const lines = csvText.split(/\r?\n/).filter((line) => line.trim().length > 0);
        if (lines.length < 2)
            throw new Error("CSV replay requires a header and at least one sample row.");
        const headers = lines[0]?.split(",").map((header) => header.trim()) ?? [];
        const columns = ["nodeId", "seq", "tStartMs", "sampleRateHz", "vCounts", "iCounts"];
        const positions = columns.map((column) => headers.indexOf(column));
        const frameIdPosition = headers.indexOf("frameId");
        if (positions.some((position) => position < 0)) {
            throw new Error(`CSV replay header must include: ${columns.join(", ")}.`);
        }
        const grouped = new Map();
        for (let lineIndex = 1; lineIndex < lines.length; lineIndex += 1) {
            const cells = lines[lineIndex]?.split(",") ?? [];
            const values = positions.map((position) => Number(cells[position]));
            if (values.some((value) => !Number.isFinite(value))) {
                throw new Error(`Invalid numeric value in CSV row ${lineIndex + 1}.`);
            }
            const nodeId = values[0] ?? Number.NaN;
            const seq = values[1] ?? Number.NaN;
            const tStartMs = values[2] ?? Number.NaN;
            const sampleRateHz = values[3] ?? Number.NaN;
            const vCount = values[4] ?? Number.NaN;
            const iCount = values[5] ?? Number.NaN;
            if (!Number.isInteger(nodeId) || nodeId < 0 || nodeId > 0xffff
                || !Number.isInteger(seq) || seq < 0 || seq > 0xffff
                || !(sampleRateHz > 0)
                || !Number.isInteger(vCount) || vCount < -0x80000000 || vCount > 0x7fffffff
                || !Number.isInteger(iCount) || iCount < -0x80000000 || iCount > 0x7fffffff) {
                throw new Error(`Invalid frame metadata or ADC count in CSV row ${lineIndex + 1}.`);
            }
            const frameId = frameIdPosition < 0 ? "" : (cells[frameIdPosition] ?? "").trim();
            if (frameIdPosition >= 0 && frameId.length === 0) {
                throw new Error(`Missing frameId in CSV row ${lineIndex + 1}.`);
            }
            const key = frameIdPosition >= 0
                ? `${nodeId}:${frameId}`
                : `${nodeId}:${seq}:${tStartMs}:${sampleRateHz}`;
            let group = grouped.get(key);
            if (!group) {
                group = {
                    frame: { nodeId, seq, tStartMs, sampleRateHz, vCounts: new Int32Array(0), iCounts: new Int32Array(0) },
                    v: [],
                    i: [],
                };
                grouped.set(key, group);
            }
            group.v.push(vCount);
            group.i.push(iCount);
        }
        this.frames = Array.from(grouped.values(), ({ frame, v, i }) => ({
            ...frame,
            vCounts: Int32Array.from(v),
            iCounts: Int32Array.from(i),
        }));
    }
    next() {
        const frame = this.frames[this.cursor];
        if (!frame)
            return null;
        this.cursor += 1;
        return frame;
    }
}
export class ArrayFrameSource {
    frames;
    cursor = 0;
    constructor(frames) {
        this.frames = frames;
    }
    next() {
        const frame = this.frames[this.cursor];
        if (!frame)
            return null;
        this.cursor += 1;
        return frame;
    }
}
