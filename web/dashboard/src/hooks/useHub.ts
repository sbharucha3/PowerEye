import { useCallback, useEffect, useRef, useState } from "react";
import { fetchHistory, fetchNodes, syncHubClock } from "@/lib/api";
import { mockHistory, mockNodes } from "@/lib/mock";
import { deriveState, STATE_LABEL, type NodeState } from "@/lib/status";
import {
  RANGE_MINUTES,
  type HistoryResponse,
  type NodesResponse,
  type RangeKey,
  type SeriesPoint,
  type Snapshot,
} from "@/lib/types";

export interface HubEvent {
  t: number;
  node: number;
  state: NodeState | "seen";
  text: string;
}

const POLL_MS = 2500;
const HISTORY_MS = 6000;
const MAX_FAILS = 3;
const MAX_SNAPSHOTS = 4000;
const ENV_MOCK = import.meta.env.VITE_MOCK === "1";

function toSeries(v: HistoryResponse, a: HistoryResponse): Record<number, SeriesPoint[]> {
  const out: Record<number, SeriesPoint[]> = {};
  const nowWall = Date.now();
  const point = (h: HistoryResponse, j: number) => nowWall - (h.now - (h.t0 + j * h.step));
  for (const s of v.series) {
    const aSeries = a.series.find((x) => x.id === s.id);
    out[s.id] = s.v.map((val, j) => ({
      t: point(v, j),
      v: val,
      a: aSeries ? (aSeries.v[j] ?? null) : null,
    }));
  }
  return out;
}

export function useHub(range: RangeKey) {
  const [forceDemo, setForceDemo] = useState(ENV_MOCK);
  const [fails, setFails] = useState(0);
  const [nodes, setNodes] = useState<NodesResponse | null>(null);
  const [series, setSeries] = useState<Record<number, SeriesPoint[]>>({});
  const [snapshots, setSnapshots] = useState<Record<number, Snapshot[]>>({});
  const [events, setEvents] = useState<HubEvent[]>([]);
  const [lastUpdate, setLastUpdate] = useState<number | null>(null);
  const [historyLoaded, setHistoryLoaded] = useState(false);

  const failsRef = useRef(0);
  const forceRef = useRef(forceDemo);
  const prevState = useRef<Record<number, NodeState>>({});
  const clockSynced = useRef(false);
  const busy = useRef(false);

  forceRef.current = forceDemo;

  const usingDemo = forceDemo || fails >= MAX_FAILS;

  const tick = useCallback(async () => {
    if (busy.current) return;
    busy.current = true;
    try {
      let data: NodesResponse | null = null;
      if (forceRef.current) {
        data = mockNodes();
      } else {
        try {
          data = await fetchNodes();
          failsRef.current = 0;
          setFails(0);
          if (!clockSynced.current) {
            clockSynced.current = true;
            void syncHubClock();
          }
        } catch {
          failsRef.current += 1;
          setFails(failsRef.current);
          if (failsRef.current >= MAX_FAILS) data = mockNodes();
        }
      }
      if (!data) return;
      const now = Date.now();
      setNodes(data);
      setLastUpdate(now);
      setSnapshots((prev) => {
        const next = { ...prev };
        for (const n of data!.nodes) {
          const list = (next[n.id] ?? []).concat({
            t: now,
            online: n.age <= data!.timeout,
            rssi: n.rssi,
            snr: n.snr,
            nrssi: n.nrssi,
            nsnr: n.nsnr,
          });
          next[n.id] = list.length > MAX_SNAPSHOTS ? list.slice(-MAX_SNAPSHOTS) : list;
        }
        return next;
      });
      const fresh: HubEvent[] = [];
      for (const n of data.nodes) {
        const st = deriveState(n, data.timeout);
        const was = prevState.current[n.id];
        if (was === undefined) {
          fresh.push({ t: now, node: n.id, state: "seen", text: `Node ${n.id} detected (${STATE_LABEL[st]})` });
        } else if (was !== st) {
          const text =
            st === "offline"
              ? `Node ${n.id} went offline`
              : was === "offline"
                ? `Node ${n.id} back online (${STATE_LABEL[st]})`
                : `Node ${n.id}: ${STATE_LABEL[was]} to ${STATE_LABEL[st]}`;
          fresh.push({ t: now, node: n.id, state: st, text });
        }
        prevState.current[n.id] = st;
      }
      if (fresh.length) setEvents((e) => [...fresh.reverse(), ...e].slice(0, 60));
    } finally {
      busy.current = false;
    }
  }, []);

  useEffect(() => {
    void tick();
    const id = setInterval(() => void tick(), POLL_MS);
    return () => clearInterval(id);
  }, [tick]);

  // Charts: refetch when the range, the source or the node list changes, and every few seconds.
  const hasNodes = nodes !== null;
  useEffect(() => {
    if (!hasNodes) return;
    let cancelled = false;
    const minutes = RANGE_MINUTES[range];
    const load = async () => {
      try {
        const demo = forceRef.current || failsRef.current >= MAX_FAILS;
        const [v, a] = demo
          ? [mockHistory("v", minutes), mockHistory("a", minutes)]
          : await Promise.all([fetchHistory("v", minutes), fetchHistory("a", minutes)]);
        if (cancelled) return;
        setSeries(toSeries(v, a));
        setHistoryLoaded(true);
      } catch {
        /* keep previous series; the next tick retries */
      }
    };
    setHistoryLoaded(false);
    void load();
    const id = setInterval(() => void load(), HISTORY_MS);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [range, hasNodes, usingDemo]);

  const toggleDemo = useCallback(() => {
    setForceDemo((d) => !d);
    failsRef.current = 0;
    setFails(0);
    setHistoryLoaded(false);
  }, []);

  return {
    nodes,
    series,
    snapshots,
    events,
    lastUpdate,
    historyLoaded,
    usingDemo,
    forcedDemo: forceDemo,
    hubUnreachable: !forceDemo && fails >= MAX_FAILS,
    connecting: nodes === null,
    toggleDemo,
  };
}
