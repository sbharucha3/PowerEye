import type { EventPacket, SummaryPacket, WindowResult } from "./types.js";
export declare const SUMMARY_PACKET_BYTES = 24;
export declare const EVENT_PACKET_TYPE = 1;
export declare function downsampleEventWaveform(waveform: Int16Array, maximumPairs?: number): Int16Array;
/** Aggregates a minute (or other reporting interval) before SUMMARY encoding. */
export declare function aggregateSummary(windows: readonly WindowResult[], timestampSeconds: number, seq: number, batteryPercent?: number): SummaryPacket;
/** SUMMARY quantization: V 0.01 V; I 1 mA; f 1 mHz; P/S 0.1; THD 0.01%; E 0.1 Wh. */
export declare function encodeSummaryPacket(packet: SummaryPacket): Uint8Array;
export declare function decodeSummaryPacket(bytes: Uint8Array): SummaryPacket;
/** EVENT format is provisional: 14-byte header followed by up to 64 signed V/I sample pairs. */
export declare function encodeEventPacket(packet: EventPacket): Uint8Array;
export declare function decodeEventPacket(bytes: Uint8Array): EventPacket;
