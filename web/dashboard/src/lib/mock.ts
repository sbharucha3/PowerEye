import type { HistoryResponse, NodeInfo, NodesResponse } from "./types";
import { STATUS_BATTERY, STATUS_BAD, STATUS_GOOD, STATUS_TRIP } from "./status";

// Demo data in exactly the same JSON shapes the hub serves. Every value is a pure
// function of time, so history and the live view always agree.

const SAMPLE_MS = 3000;
const BOOT = Date.now() - 20 * 60 * 1000; // pretend the node has been up 20 minutes

function hash(n: number): number {
  const x = Math.sin(n * 12.9898) * 43758.5453;
  return x - Math.floor(x);
}

interface Sample {
  online: boolean;
  v: number;
  a: number;
  st: number;
}

/** Scripted story: mostly good, one bad-signal episode, one trip, one dropout. */
function sampleAt(t: number): Sample {
  const slot = Math.floor(t / SAMPLE_MS);
  const phase = ((t - BOOT) / 1000) % 600; // 10 minute loop
  const noise = (hash(slot) - 0.5) * 2;
  const online = !(phase > 430 && phase < 460);
  let v = 119.8 + 0.9 * Math.sin(t / 9000) + noise * 0.35;
  let a = 15.0 + 0.6 * Math.sin(t / 15000 + 1) + noise * 0.25;
  let st = STATUS_GOOD;
  if (phase > 250 && phase < 290) {
    v = 104 + noise * 1.2; // sag
    st = STATUS_BAD;
  }
  if (phase > 330 && phase < 345) {
    a = 27 + noise * 0.8; // overcurrent -> trip
    st = STATUS_TRIP;
  }
  if (phase > 520 && phase < 560) st |= STATUS_BATTERY;
  return { online, v, a, st };
}

export function mockNodes(): NodesResponse {
  const now = Date.now();
  const lastSlotT = Math.floor(now / SAMPLE_MS) * SAMPLE_MS;
  // last time the node was online
  let tOnline = lastSlotT;
  while (!sampleAt(tOnline).online && now - tOnline < 3600000) tOnline -= SAMPLE_MS;
  const s = sampleAt(tOnline);
  const age = (now - tOnline) / 1000 + (hash(Math.floor(now / 2500)) * 1.5);
  const uptimeSlots = Math.floor((now - BOOT) / 5000);
  const polls = uptimeSlots;
  const replies = Math.floor(uptimeSlots * 0.978);
  const rxTotal = Math.floor((now - BOOT) / SAMPLE_MS);
  const node: NodeInfo = {
    id: 1,
    age,
    v: Math.round(s.v * 100) / 100,
    a: Math.round(s.a * 100) / 100,
    w: Math.round(s.v * s.a * 10) / 10,
    t: 31.5 + 0.8 * Math.sin(now / 60000),
    st: s.st,
    rssi: -71 + Math.round(hash(Math.floor(now / 5000)) * 6) - 3,
    snr: 9.5 + (hash(Math.floor(now / 5000) + 3) - 0.5) * 2,
    nrssi: -69 + Math.round(hash(Math.floor(now / 5000) + 9) * 5) - 2,
    nsnr: 9 + (hash(Math.floor(now / 5000) + 5) - 0.5) * 2,
    polls,
    replies,
    rx: rxTotal - 7,
    lost: 7,
    backfilled: 5,
    backlog: 0,
    buffered: Math.min(1200, rxTotal),
    chip: 42,
    resets: 0,
  };
  return {
    cycle: uptimeSlots,
    polled: 1,
    answered: age > 15 ? 0 : 1,
    retries: 0,
    ms: 70 + Math.round(hash(Math.floor(now / 5000)) * 12),
    timeout: 15,
    sample: SAMPLE_MS,
    clock: "demo clock",
    hubChip: 47,
    hubHot: false,
    hubRadioResets: 0,
    nodes: [node],
  };
}

export function mockHistory(metric: "v" | "a" | "t" | "w", minutes: number): HistoryResponse {
  const now = Date.now();
  const maxPoints = 600;
  const slots = Math.floor((minutes * 60000) / SAMPLE_MS);
  const k = Math.max(1, Math.ceil(slots / maxPoints));
  const npts = Math.ceil(slots / k);
  const step = k * SAMPLE_MS;
  const endSlot = Math.floor(now / SAMPLE_MS);
  const startSlot = endSlot - slots + 1;
  const values: (number | null)[] = [];
  for (let j = 0; j < npts; j++) {
    let sum = 0;
    let cnt = 0;
    for (let s = startSlot + j * k; s < startSlot + (j + 1) * k && s <= endSlot; s++) {
      const sm = sampleAt(s * SAMPLE_MS);
      if (!sm.online) continue;
      sum += metric === "v" ? sm.v : metric === "a" ? sm.a : metric === "t" ? 31.5 : sm.v * sm.a;
      cnt++;
    }
    values.push(cnt ? Math.round((sum / cnt) * 100) / 100 : null);
  }
  return {
    t0: startSlot * SAMPLE_MS,
    step,
    n: npts,
    now: now,
    off: 0,
    real: true,
    series: [{ id: 1, v: values }],
  };
}
