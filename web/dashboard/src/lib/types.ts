// Exact shapes served by the hub (src/lora_star/hub.ino).

/** One entry of GET /api/nodes -> nodes[] */
export interface NodeInfo {
  id: number;
  age: number; // seconds since the hub last heard this node
  v: number; // volts (line-equivalent)
  a: number; // amps (line-equivalent)
  w: number; // watts
  t: number; // chip temperature at the node, deg C (sensor temp field)
  st: number; // status flags, see status.ts
  rssi: number; // hub's view of the node, dBm
  snr: number;
  nrssi: number; // node's view of the hub, dBm
  nsnr: number;
  polls: number;
  replies: number;
  rx: number; // samples received
  lost: number; // samples lost
  backfilled: number;
  backlog: number;
  buffered: number;
  chip: number; // node ESP32 chip temp
  resets: number; // node radio resets
}

/** GET /api/nodes */
export interface NodesResponse {
  cycle: number;
  polled: number;
  answered: number;
  retries: number;
  ms: number;
  timeout: number; // seconds before a node is shown offline
  sample: number; // sample period, ms
  clock: string;
  hubChip: number;
  hubHot: boolean;
  hubRadioResets: number;
  nodes: NodeInfo[];
}

/** GET /api/history?m=v|a|t|w&min=N. Point j happens at net time t0 + j*step (ms). */
export interface HistoryResponse {
  t0: number;
  step: number;
  n: number;
  now: number; // hub network clock now, ms
  off: number;
  real: boolean;
  series: { id: number; v: (number | null)[] }[];
}

export type Mode = "live" | "demo";
export type RangeKey = "5m" | "1h" | "8h";
export const RANGE_MINUTES: Record<RangeKey, number> = { "5m": 5, "1h": 60, "8h": 480 };

/** One point the charts draw. t is a wall-clock epoch in ms. */
export interface SeriesPoint {
  t: number;
  v: number | null;
  a: number | null;
}

/** One client-side snapshot of a node, kept for the link and event views. */
export interface Snapshot {
  t: number;
  online: boolean;
  rssi: number;
  snr: number;
  nrssi: number;
  nsnr: number;
}
