#!/usr/bin/env python3
"""PowerEye ingestion pipeline for Shelly Plug US Gen4 (Gen2+ RPC API).

Polls any number of plugs in parallel, keeps every device's time series in
memory, logs each sample to CSV, and exposes the data as pandas DataFrames.
No database yet: add one later by writing another sink (see CsvSink).

Quick start
-----------
    # no hardware yet: two simulated plugs (A = source end, B = load end)
    python powereye_ingest.py --mock --duration 30

    # real plugs (find IPs in your router or the Shelly app)
    python powereye_ingest.py --device A=192.168.1.50 --device B=192.168.1.51

Use it from your own code
-------------------------
    from powereye_ingest import Ingestor, ShellyHttpSource
    ing = Ingestor({"A": ShellyHttpSource("192.168.1.50"),
                    "B": ShellyHttpSource("192.168.1.51")}, interval=1.0)
    ing.start()
    ...
    df = ing.store.to_frame()                 # one row per sample, all devices
    volts = ing.store.aligned("voltage")      # one column per device, 1 s grid
    drop = volts["A"] - volts["B"]

Design rules
------------
* A failed poll is stored as a gap (ok=False, values empty). It is never
  stored as zero, so "no data" cannot be mistaken for "no load".
* seq increments on every attempt, so missing samples are visible.
* Fields the device does not report (e.g. freq, pf) are stored as None.
* *_calc fields are computed here, not measured by the plug.

Needs only the Python standard library. pandas is optional (for DataFrames).
"""
from __future__ import annotations

import argparse
import csv
import json
import random
import threading
import time
import urllib.error
import urllib.request
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Callable, Optional

# Columns stored for every sample, in CSV order.
FIELDS = (
    "output",      # relay state (True = on)
    "voltage",     # V, measured
    "current",     # A, measured
    "apower",      # W, active power, measured
    "freq",        # Hz, only if the device reports it
    "pf",          # power factor, only if the device reports it
    "energy_wh",   # Wh, cumulative counter from the device
    "temp_c",      # internal temperature
    "va_calc",     # VA = V x I, calculated here
    "pf_calc",     # W / VA, calculated here
)


@dataclass
class Sample:
    ts: float                      # unix seconds (this machine's clock)
    device: str
    seq: int
    ok: bool
    rtt_ms: Optional[float] = None
    error: Optional[str] = None
    values: dict = field(default_factory=dict)

    def row(self) -> list:
        vals = [self.values.get(f) for f in FIELDS]
        return [f"{self.ts:.3f}", self.device, self.seq, int(self.ok),
                "" if self.rtt_ms is None else f"{self.rtt_ms:.1f}",
                self.error or ""] + ["" if v is None else v for v in vals]


# --------------------------------------------------------------------------
# Parsing: Switch.GetStatus response -> flat dict
# --------------------------------------------------------------------------
def parse_switch_status(raw: dict) -> dict:
    """Pull known fields out of a Switch.GetStatus reply, tolerating gaps."""
    v, i, p = raw.get("voltage"), raw.get("current"), raw.get("apower")
    out = {
        "output": raw.get("output"),
        "voltage": v,
        "current": i,
        "apower": p,
        "freq": raw.get("freq"),
        "pf": raw.get("pf"),
        "energy_wh": (raw.get("aenergy") or {}).get("total"),
        "temp_c": (raw.get("temperature") or {}).get("tC"),
    }
    if v is not None and i is not None:
        va = v * i
        out["va_calc"] = va
        if p is not None and va > 1.0:          # PF is meaningless at ~0 load
            out["pf_calc"] = min(abs(p) / va, 1.0)
    return out


# --------------------------------------------------------------------------
# Sources: anything with .fetch() -> raw Switch.GetStatus dict
# --------------------------------------------------------------------------
class ShellyHttpSource:
    """Polls http://<host>/rpc/Switch.GetStatus?id=<switch_id>."""

    def __init__(self, host: str, switch_id: int = 0, timeout: float = 2.0):
        self.url = f"http://{host}/rpc/Switch.GetStatus?id={switch_id}"
        self.timeout = timeout

    def fetch(self) -> dict:
        with urllib.request.urlopen(self.url, timeout=self.timeout) as r:
            return json.load(r)


class MockPlug:
    """Simulated plug. Same reply shape as the real API, so the real parse
    path is exercised. Timeline (seconds since t0): load on 5-25 s, and an
    optional offline window to demo gaps. drop_per_amp fakes cable loss."""

    def __init__(self, t0: float, drop_per_amp: float = 0.0,
                 offline: Optional[tuple] = None, load_amps: float = 12.0):
        self.t0, self.drop, self.offline, self.load = t0, drop_per_amp, offline, load_amps
        self.wh, self.last = 0.0, time.time()

    def fetch(self) -> dict:
        now = time.time()
        t = now - self.t0
        if self.offline and self.offline[0] <= t < self.offline[1]:
            raise urllib.error.URLError("mock device offline")
        amps = self.load + random.gauss(0, 0.05) if 5 <= t < 25 else 0.0
        volts = 120.0 - self.drop * amps + random.gauss(0, 0.15)
        watts = volts * amps * 0.99
        self.wh += watts * (now - self.last) / 3600.0
        self.last = now
        return {"id": 0, "source": "mock", "output": True,
                "apower": round(watts, 1), "voltage": round(volts, 1),
                "current": round(amps, 3), "freq": round(60.0 + random.gauss(0, 0.01), 2),
                "aenergy": {"total": round(self.wh, 3)},
                "temperature": {"tC": 38.0, "tF": 100.4}}


# --------------------------------------------------------------------------
# Store: per-device time series, thread-safe, in memory
# --------------------------------------------------------------------------
class TimeSeriesStore:
    def __init__(self, maxlen: int = 86_400):     # ~1 day per device at 1 Hz
        self._data: dict[str, deque] = {}
        self._maxlen = maxlen
        self._lock = threading.Lock()

    def append(self, s: Sample) -> None:
        with self._lock:
            self._data.setdefault(s.device, deque(maxlen=self._maxlen)).append(s)

    def devices(self) -> list[str]:
        with self._lock:
            return sorted(self._data)

    def latest(self, device: str) -> Optional[Sample]:
        with self._lock:
            d = self._data.get(device)
            return d[-1] if d else None

    def samples(self, device: str, since: Optional[float] = None) -> list[Sample]:
        with self._lock:
            items = list(self._data.get(device, ()))
        return [s for s in items if since is None or s.ts >= since]

    def to_frame(self, device: Optional[str] = None):
        """One row per sample, indexed by time. Gaps appear as ok=False rows
        with NaN values."""
        import pandas as pd
        names = [device] if device else self.devices()
        rows = []
        for n in names:
            for s in self.samples(n):
                rows.append({"time": pd.to_datetime(s.ts, unit="s"), "device": s.device,
                             "seq": s.seq, "ok": s.ok, "rtt_ms": s.rtt_ms,
                             **{f: s.values.get(f) for f in FIELDS}})
        if not rows:
            return pd.DataFrame(columns=["device", "seq", "ok", "rtt_ms", *FIELDS])
        return pd.DataFrame(rows).set_index("time").sort_index()

    def aligned(self, metric: str, freq: str = "1s"):
        """One column per device on a common time grid. Missing data stays
        NaN (never zero), so dropouts are visible when you compare devices."""
        import pandas as pd
        df = self.to_frame()
        df = df[df["ok"]]
        if df.empty:
            return pd.DataFrame()
        df[metric] = pd.to_numeric(df[metric], errors="coerce")
        return df.groupby([pd.Grouper(freq=freq), "device"])[metric].mean().unstack("device")


# --------------------------------------------------------------------------
# Sinks: any callable(Sample). Add a database sink here later.
# --------------------------------------------------------------------------
class CsvSink:
    HEADER = ["ts", "device", "seq", "ok", "rtt_ms", "error", *FIELDS]

    def __init__(self, directory: str = "data"):
        Path(directory).mkdir(parents=True, exist_ok=True)
        self.path = Path(directory) / f"session_{datetime.now():%Y%m%d_%H%M%S}.csv"
        self._f = open(self.path, "w", newline="")
        self._w = csv.writer(self._f)
        self._w.writerow(self.HEADER)
        self._lock = threading.Lock()

    def __call__(self, s: Sample) -> None:
        with self._lock:
            self._w.writerow(s.row())
            self._f.flush()

    def close(self) -> None:
        self._f.close()


# --------------------------------------------------------------------------
# Poller: one thread per device
# --------------------------------------------------------------------------
class DevicePoller(threading.Thread):
    def __init__(self, name: str, source, interval: float,
                 emit: Callable[[Sample], None], stop: threading.Event):
        super().__init__(daemon=True, name=f"poll-{name}")
        self.dev, self.source, self.interval = name, source, interval
        self.emit, self._stop_evt = emit, stop

    def run(self) -> None:
        seq, next_t = 0, time.monotonic()
        while not self._stop_evt.is_set():
            t_wall, t0 = time.time(), time.monotonic()
            seq += 1                                   # counts failed attempts too
            try:
                raw = self.source.fetch()
                rtt = (time.monotonic() - t0) * 1000.0
                # timestamp at the midpoint of the request
                s = Sample(ts=t_wall + rtt / 2000.0, device=self.dev, seq=seq,
                           ok=True, rtt_ms=rtt, values=parse_switch_status(raw))
            except (urllib.error.URLError, TimeoutError, OSError,
                    ValueError, KeyError) as e:
                s = Sample(ts=t_wall, device=self.dev, seq=seq, ok=False,
                           error=type(e).__name__ + ": " + str(e)[:80])
            self.emit(s)
            next_t += self.interval                    # fixed cadence, no drift
            wait = next_t - time.monotonic()
            if wait < 0:                               # fell behind: don't burst
                next_t = time.monotonic()
                wait = 0
            self._stop_evt.wait(wait)


# --------------------------------------------------------------------------
# Ingestor: wires sources -> store + sinks
# --------------------------------------------------------------------------
class Ingestor:
    def __init__(self, sources: dict, interval: float = 1.0,
                 sinks: Optional[list] = None, store: Optional[TimeSeriesStore] = None):
        self.store = store or TimeSeriesStore()
        self.sinks = list(sinks or [])
        self._stop = threading.Event()
        self._pollers = [DevicePoller(n, src, interval, self._emit, self._stop)
                         for n, src in sources.items()]

    def _emit(self, s: Sample) -> None:
        self.store.append(s)
        for sink in self.sinks:
            try:
                sink(s)
            except Exception as e:                     # a bad sink must not kill polling
                print(f"[sink error] {type(sink).__name__}: {e}")

    def start(self) -> None:
        for p in self._pollers:
            p.start()

    def stop(self) -> None:
        self._stop.set()
        for p in self._pollers:
            p.join(timeout=3)
        for sink in self.sinks:
            if hasattr(sink, "close"):
                sink.close()


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def _fmt(x, spec: str, width: int) -> str:
    return f"{x:{spec}}".rjust(width) if isinstance(x, (int, float)) else "--".rjust(width)


def _print_status(store: TimeSeriesStore) -> None:
    now = time.time()
    print(f"--- {datetime.now():%H:%M:%S} ---")
    for d in store.devices():
        s = store.latest(d)
        if s is None:
            continue
        age = now - s.ts
        if s.ok:
            v = s.values
            print(f"{d:>8}  V={_fmt(v.get('voltage'), '.1f', 6)}  I={_fmt(v.get('current'), '.2f', 6)}"
                  f"  P={_fmt(v.get('apower'), '.0f', 6)} W  age={age:.1f}s")
        else:
            print(f"{d:>8}  NO DATA ({age:.0f}s since last attempt)  {s.error}")


def main() -> None:
    ap = argparse.ArgumentParser(description="PowerEye Shelly Plug ingestion")
    ap.add_argument("--device", action="append", default=[], metavar="NAME=HOST",
                    help="repeatable, e.g. --device A=192.168.1.50")
    ap.add_argument("--mock", action="store_true", help="simulate two plugs")
    ap.add_argument("--interval", type=float, default=1.0, help="poll seconds")
    ap.add_argument("--switch-id", type=int, default=0)
    ap.add_argument("--csv-dir", default="data")
    ap.add_argument("--report-every", type=float, default=2.0)
    ap.add_argument("--duration", type=float, default=0, help="seconds, 0 = until Ctrl+C")
    args = ap.parse_args()

    if args.mock:
        t0 = time.time()
        sources = {"A": MockPlug(t0, 0.0), "B": MockPlug(t0, 0.4, offline=(12, 16))}
    else:
        sources = {}
        for item in args.device:
            name, _, host = item.partition("=")
            if not host:
                ap.error(f"--device needs NAME=HOST, got {item!r}")
            sources[name] = ShellyHttpSource(host, args.switch_id)
        if not sources:
            ap.error("give at least one --device NAME=HOST, or use --mock")

    csv_sink = CsvSink(args.csv_dir)
    ing = Ingestor(sources, args.interval, sinks=[csv_sink])
    print(f"Logging to {csv_sink.path}")
    ing.start()
    t_end = time.time() + args.duration if args.duration else None
    try:
        while t_end is None or time.time() < t_end:
            time.sleep(args.report_every)
            _print_status(ing.store)
    except KeyboardInterrupt:
        pass
    finally:
        ing.stop()
    print(f"Stopped. {sum(len(ing.store.samples(d)) for d in ing.store.devices())} samples logged.")


if __name__ == "__main__":
    main()
