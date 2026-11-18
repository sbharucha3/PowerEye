import {
  DEMO_DEVICES,
  DEMO_HUB,
  type TelemetryService,
  type TelemetrySubscription,
} from "./telemetryService";
import type { AlertRecord, HubSnapshot, Measurement } from "./telemetry";

const STORAGE_KEY = "powereye:demo-hub:snapshot";
const CHANNEL_KEY = "powereye:demo-hub";
const HISTORY_LIMIT = 15 * 60;

function clamp(value: number, low: number, high: number) {
  return Math.max(low, Math.min(high, value));
}

function createMeasurement(deviceId: string, measuredAt: number): Measurement {
  const slots: Record<string, [number, number, number]> = {
    gen: [68.4, 239.8, 285.3],
    battery: [12.8, 240.2, 53.6],
    main: [81.2, 239.4, 339.2],
    a: [49.7, 238.6, 208.7],
    b: [31.5, 240.1, 131.2],
    key: [9.6, 238.1, 42.1],
    fill: [4.2, 238.8, 18.6],
    camera: [1.5, 239.0, 6.8],
    video: [0, 0, 0],
    sound: [0.9, 240.0, 4.2],
    craft: [6.7, 236.9, 28.9],
    trailer: [3.7, 239.7, 16.2],
  };
  const [basePower, baseVoltage, baseCurrent] = slots[deviceId] ?? [1, 240, 4];
  const wave = Math.sin(measuredAt / 9000 + DEMO_DEVICES.findIndex((d) => d.id === deviceId));
  const noise = Math.sin(measuredAt / 2300 + deviceId.length);
  const isOffline = deviceId === "video";
  if (isOffline) {
    return {
      deviceId,
      hubId: DEMO_HUB.id,
      measuredAt,
      receivedAt: null,
      displayedAt: null,
      voltage: null,
      current: null,
      power: null,
      powerFactor: null,
      frequency: null,
      signalDbm: null,
      source: "simulated",
    };
  }
  const voltage = baseVoltage + wave * 0.45 + noise * 0.12;
  const current = baseCurrent + wave * (baseCurrent * 0.012) + noise * 0.08;
  const power = basePower * 1000 * (1 + wave * 0.008);
  return {
    deviceId,
    hubId: DEMO_HUB.id,
    measuredAt,
    receivedAt: measuredAt,
    displayedAt: Date.now(),
    voltage,
    current,
    power,
    powerFactor: clamp(0.94 + wave * 0.018, 0.85, 1),
    frequency: 60 + wave * 0.08,
    signalDbm: -65 + Math.round(noise * 5),
    source: "simulated",
  };
}

export function evaluateDemoAlerts(
  previous: AlertRecord[],
  latest: Record<string, Measurement>,
): AlertRecord[] {
  const now = Math.max(...Object.values(latest).map((m) => m.measuredAt), Date.now());
  const alerts = [...previous];
  const openByTitle = new Set(alerts.filter((a) => !a.acknowledged).map((a) => `${a.deviceId}:${a.title}`));

  const maybeOpen = (deviceId: string, title: string, message: string, severity: AlertRecord["severity"]) => {
    const key = `${deviceId}:${title}`;
    if (!openByTitle.has(key)) {
      alerts.unshift({
        id: `${deviceId}-${title}-${now}`,
        hubId: DEMO_HUB.id,
        deviceId,
        createdAt: now,
        severity,
        title,
        message,
        acknowledged: false,
        demo: true,
      });
      openByTitle.add(key);
    }
  };

  const craft = latest.craft;
  if (craft?.current !== null && craft.current !== undefined && craft.current > 28.5) {
    maybeOpen("craft", "Demo high current", "Craft Services exceeded a demo-only current marker.", "critical");
  }
  const distroA = latest.a;
  if (distroA?.power !== null && distroA.power !== undefined && distroA.power > 49_000) {
    maybeOpen("a", "Demo high load", "Distro A exceeded a demo-only load marker.", "warning");
  }
  if (!latest.video?.receivedAt || now - latest.video.measuredAt > 5_000) {
    maybeOpen("video", "Demo no data", "Video Village has not received simulated measurements.", "warning");
  }
  return alerts.slice(0, 60);
}

export class MockTelemetryService implements TelemetryService {
  private listeners = new Set<(snapshot: HubSnapshot) => void>();
  private interval: number | null = null;
  private channel: BroadcastChannel | null = null;
  private snapshot: HubSnapshot;

  constructor(hubId = DEMO_HUB.id) {
    const saved = this.load();
    this.snapshot = saved?.hub.id === hubId
      ? saved
      : {
          hub: { ...DEMO_HUB, id: hubId },
          devices: DEMO_DEVICES.map((d) => ({ ...d, hubId })),
          latest: {},
          history: [],
          alerts: [],
          receivedAt: Date.now(),
        };
  }

  private load(): HubSnapshot | null {
    try {
      const value = localStorage.getItem(STORAGE_KEY);
      return value ? JSON.parse(value) as HubSnapshot : null;
    } catch {
      return null;
    }
  }

  private save() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(this.snapshot));
    } catch {
      // The live view remains usable even when browser storage is unavailable.
    }
  }

  private emit() {
    for (const listener of this.listeners) listener(this.snapshot);
  }

  private tick = () => {
    const now = Date.now();
    const latest: Record<string, Measurement> = {};
    for (const device of this.snapshot.devices) {
      latest[device.id] = createMeasurement(device.id, now);
    }
    const history = [...this.snapshot.history];
    for (const measurement of Object.values(latest)) {
      if (measurement.source === "simulated") history.push(measurement);
    }
    const alerts = evaluateDemoAlerts(this.snapshot.alerts, latest);
    this.snapshot = {
      ...this.snapshot,
      latest,
      history: history.slice(-HISTORY_LIMIT),
      alerts,
      receivedAt: now,
    };
    this.save();
    this.channel?.postMessage({ type: "snapshot", snapshot: this.snapshot });
    this.emit();
  };

  start() {
    if (this.interval !== null) return;
    if ("BroadcastChannel" in window) {
      this.channel = new BroadcastChannel(CHANNEL_KEY);
      this.channel.onmessage = (event) => {
        if (event.data?.type !== "snapshot") return;
        this.snapshot = event.data.snapshot as HubSnapshot;
        this.emit();
      };
    }
    this.tick();
    this.interval = window.setInterval(this.tick, 1000);
  }

  stop() {
    if (this.interval !== null) window.clearInterval(this.interval);
    this.interval = null;
    this.channel?.close();
    this.channel = null;
  }

  subscribe(listener: (snapshot: HubSnapshot) => void): TelemetrySubscription {
    this.listeners.add(listener);
    listener(this.snapshot);
    return { unsubscribe: () => this.listeners.delete(listener) };
  }

  getSnapshot() {
    return this.snapshot;
  }
}
