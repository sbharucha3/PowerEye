import type {
  Device,
  Hub,
  HubSnapshot,
} from "./telemetry";

export interface TelemetrySubscription {
  unsubscribe: () => void;
}

export interface TelemetryService {
  start(): void;
  stop(): void;
  subscribe(listener: (snapshot: HubSnapshot) => void): TelemetrySubscription;
  getSnapshot(): HubSnapshot;
}

export const DEMO_HUB: Hub = {
  id: "demo-hub",
  name: "Downtown Shoot, Day 3",
  source: "simulated",
  demoAccess: true,
};

export const DEMO_DEVICES: Device[] = [
  { id: "gen", hubId: DEMO_HUB.id, name: "Generator 1", role: "source", parentId: null, status: "ok", units: { voltage: "V", current: "A", power: "W" } },
  { id: "battery", hubId: DEMO_HUB.id, name: "Battery Bank", role: "source", parentId: null, status: "ok", units: { voltage: "V", current: "A", power: "W" } },
  { id: "main", hubId: DEMO_HUB.id, name: "Main Distro", role: "source", parentId: "gen", status: "ok", units: { voltage: "V", current: "A", power: "W" } },
  { id: "a", hubId: DEMO_HUB.id, name: "Distro A", role: "source", parentId: "main", status: "warning", units: { voltage: "V", current: "A", power: "W" } },
  { id: "b", hubId: DEMO_HUB.id, name: "Distro B", role: "source", parentId: "main", status: "ok", units: { voltage: "V", current: "A", power: "W" } },
  { id: "key", hubId: DEMO_HUB.id, name: "Key Light HMI", role: "load", parentId: "a", status: "warning", units: { voltage: "V", current: "A", power: "W" } },
  { id: "fill", hubId: DEMO_HUB.id, name: "Fill Light", role: "load", parentId: "a", status: "ok", units: { voltage: "V", current: "A", power: "W" } },
  { id: "camera", hubId: DEMO_HUB.id, name: "Camera Cart", role: "load", parentId: "a", status: "ok", units: { voltage: "V", current: "A", power: "W" } },
  { id: "video", hubId: DEMO_HUB.id, name: "Video Village", role: "load", parentId: "a", status: "nodata", units: { voltage: "V", current: "A", power: "W" } },
  { id: "sound", hubId: DEMO_HUB.id, name: "Sound Cart", role: "load", parentId: "b", status: "ok", units: { voltage: "V", current: "A", power: "W" } },
  { id: "craft", hubId: DEMO_HUB.id, name: "Craft Services", role: "load", parentId: "b", status: "critical", units: { voltage: "V", current: "A", power: "W" } },
  { id: "trailer", hubId: DEMO_HUB.id, name: "Trailer 1", role: "load", parentId: "b", status: "ok", units: { voltage: "V", current: "A", power: "W" } },
];
