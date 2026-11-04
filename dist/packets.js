export const SUMMARY_PACKET_BYTES = 24;
export const EVENT_PACKET_TYPE = 1;
const EVENT_HEADER_BYTES = 14;
export function downsampleEventWaveform(waveform, maximumPairs = 64) {
    if (waveform.length % 2 !== 0 || !Number.isInteger(maximumPairs) || maximumPairs < 0) {
        throw new RangeError("Event waveform must contain V/I pairs and a non-negative integer limit.");
    }
    const pairCount = Math.floor(waveform.length / 2);
    const outputPairs = Math.max(0, Math.min(64, Math.floor(maximumPairs), pairCount));
    const result = new Int16Array(outputPairs * 2);
    if (outputPairs === 0)
        return result;
    for (let pair = 0; pair < outputPairs; pair += 1) {
        const sourcePair = outputPairs === 1
            ? 0
            : Math.round((pair * (pairCount - 1)) / (outputPairs - 1));
        result[pair * 2] = waveform[sourcePair * 2] ?? 0;
        result[pair * 2 + 1] = waveform[sourcePair * 2 + 1] ?? 0;
    }
    return result;
}
function mean(values) {
    let total = 0;
    let count = 0;
    for (const value of values) {
        if (value !== null && Number.isFinite(value)) {
            total += value;
            count += 1;
        }
    }
    return count === 0 ? 0 : total / count;
}
/** Aggregates a minute (or other reporting interval) before SUMMARY encoding. */
export function aggregateSummary(windows, timestampSeconds, seq, batteryPercent) {
    if (windows.length === 0)
        throw new RangeError("Cannot aggregate an empty window list.");
    let flags = 0;
    for (const window of windows)
        flags |= window.flags;
    let latestBattery = batteryPercent;
    if (latestBattery === undefined) {
        for (let index = windows.length - 1; index >= 0; index -= 1) {
            const value = windows[index]?.batteryPercent;
            if (value !== null && value !== undefined) {
                latestBattery = value;
                break;
            }
        }
    }
    const last = windows[windows.length - 1];
    return {
        timestampSeconds,
        vrms: mean(windows.map((window) => window.vrms)),
        irms: mean(windows.map((window) => window.irms)),
        frequencyHz: mean(windows.map((window) => window.freqHz)),
        pReal: mean(windows.map((window) => window.pReal)),
        sApparent: mean(windows.map((window) => window.sApparent)),
        thdVPercent: mean(windows.map((window) => window.thdV)),
        energyWh: last.energyWh,
        flags,
        batteryPercent: latestBattery ?? 0,
        seq,
    };
}
function clampUnsigned(value, maximum) {
    if (!Number.isFinite(value))
        throw new RangeError("Packet numeric fields must be finite.");
    return Math.max(0, Math.min(maximum, Math.round(value)));
}
/** SUMMARY quantization: V 0.01 V; I 1 mA; f 1 mHz; P/S 0.1; THD 0.01%; E 0.1 Wh. */
export function encodeSummaryPacket(packet) {
    const bytes = new Uint8Array(SUMMARY_PACKET_BYTES);
    const view = new DataView(bytes.buffer);
    view.setUint32(0, clampUnsigned(packet.timestampSeconds, 0xffffffff), true);
    view.setUint16(4, clampUnsigned(packet.vrms * 100, 0xffff), true);
    view.setUint16(6, clampUnsigned(packet.irms * 1000, 0xffff), true);
    view.setUint16(8, clampUnsigned(packet.frequencyHz * 1000, 0xffff), true);
    // The provisional wire field is unsigned; negative/regenerative power saturates at zero.
    view.setUint16(10, clampUnsigned(packet.pReal * 10, 0xffff), true);
    view.setUint16(12, clampUnsigned(packet.sApparent * 10, 0xffff), true);
    view.setUint16(14, clampUnsigned(packet.thdVPercent * 100, 0xffff), true);
    view.setUint32(16, clampUnsigned(packet.energyWh * 10, 0xffffffff), true);
    view.setUint16(20, clampUnsigned(packet.flags, 0xffff), true);
    view.setUint8(22, clampUnsigned(packet.batteryPercent, 100));
    view.setUint8(23, clampUnsigned(packet.seq, 0xff));
    return bytes;
}
export function decodeSummaryPacket(bytes) {
    if (bytes.length !== SUMMARY_PACKET_BYTES) {
        throw new RangeError(`SUMMARY packet must be exactly ${SUMMARY_PACKET_BYTES} bytes.`);
    }
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    return {
        timestampSeconds: view.getUint32(0, true),
        vrms: view.getUint16(4, true) / 100,
        irms: view.getUint16(6, true) / 1000,
        frequencyHz: view.getUint16(8, true) / 1000,
        pReal: view.getUint16(10, true) / 10,
        sApparent: view.getUint16(12, true) / 10,
        thdVPercent: view.getUint16(14, true) / 100,
        energyWh: view.getUint32(16, true) / 10,
        flags: view.getUint16(20, true),
        batteryPercent: view.getUint8(22),
        seq: view.getUint8(23),
    };
}
/** EVENT format is provisional: 14-byte header followed by up to 64 signed V/I sample pairs. */
export function encodeEventPacket(packet) {
    const waveform = downsampleEventWaveform(packet.waveform ?? new Int16Array(0));
    const samplePairs = waveform.length / 2;
    const bytes = new Uint8Array(EVENT_HEADER_BYTES + samplePairs * 4);
    const view = new DataView(bytes.buffer);
    view.setUint8(0, packet.type & 0xff);
    view.setUint16(1, clampUnsigned(packet.nodeId, 0xffff), true);
    view.setUint32(3, clampUnsigned(packet.timestampSeconds, 0xffffffff), true);
    view.setUint16(7, clampUnsigned(packet.flags, 0xffff), true);
    view.setUint16(9, clampUnsigned(packet.peakVoltage * 100, 0xffff), true);
    view.setUint16(11, clampUnsigned(packet.peakCurrent * 1000, 0xffff), true);
    view.setUint8(13, samplePairs);
    for (let index = 0; index < samplePairs * 2; index += 1) {
        const sample = Math.max(-32768, Math.min(32767, Math.round(waveform[index] ?? 0)));
        view.setInt16(EVENT_HEADER_BYTES + index * 2, sample, true);
    }
    return bytes;
}
export function decodeEventPacket(bytes) {
    if (bytes.length < EVENT_HEADER_BYTES)
        throw new RangeError("EVENT packet is shorter than its header.");
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const samplePairs = view.getUint8(13);
    if (samplePairs > 64 || bytes.length !== EVENT_HEADER_BYTES + samplePairs * 4) {
        throw new RangeError("EVENT waveform length does not match its sample-pair count.");
    }
    const waveform = samplePairs > 0 ? new Int16Array(samplePairs * 2) : undefined;
    if (waveform) {
        for (let index = 0; index < waveform.length; index += 1) {
            waveform[index] = view.getInt16(EVENT_HEADER_BYTES + index * 2, true);
        }
    }
    return {
        type: view.getUint8(0),
        nodeId: view.getUint16(1, true),
        timestampSeconds: view.getUint32(3, true),
        flags: view.getUint16(7, true),
        peakVoltage: view.getUint16(9, true) / 100,
        peakCurrent: view.getUint16(11, true) / 1000,
        waveform,
    };
}
