import type { HistoryResponse, NodesResponse } from "./types";

// All hub calls go through the Vite dev proxy (/api -> http://192.168.4.1).

async function getJson<T>(url: string, timeoutMs = 3500): Promise<T> {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), timeoutMs);
  try {
    const res = await fetch(url, { signal: ctl.signal, cache: "no-store" });
    if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
    return (await res.json()) as T;
  } finally {
    clearTimeout(timer);
  }
}

export const fetchNodes = () => getJson<NodesResponse>("/api/nodes");

/** metric: v = volts, a = amps, t = temp, anything else = watts */
export const fetchHistory = (metric: "v" | "a" | "t" | "w", minutes: number) =>
  getJson<HistoryResponse>(`/api/history?m=${metric}&min=${minutes}`, 6000);

/** Tell the hub the wall-clock time (it only accepts this once per hub boot). */
export async function syncHubClock(): Promise<void> {
  try {
    await fetch(`/api/time?ms=${Date.now()}`, { cache: "no-store" });
  } catch {
    /* best effort */
  }
}

export async function identifyNode(id: number): Promise<void> {
  await fetch(`/api/cmd?id=${id}&c=identify`, { cache: "no-store" });
}
