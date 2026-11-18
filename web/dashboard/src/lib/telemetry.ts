export type DataSource = "simulated" | "hardware";

export interface Hub {
  id: string;
  name: string;
  source: DataSource;
  demoAccess: boolean;
}

export interface Device {
  id: string;
  hubId: string;
  name: string;
  role: "source" | "load";
  parentId: string | null;
  status: "ok" | "warning" | "critical" | "nodata";
  units: {
    voltage: "V";
    current: "A";
    power: "W";
  };
}

export interface Measurement {
  deviceId: string;
  hubId: string;
  measuredAt: number;
  receivedAt: number | null;
  displayedAt: number | null;
  voltage: number | null;
  current: number | null;
  power: number | null;
  powerFactor: number | null;
  frequency: number | null;
  signalDbm: number | null;
  source: DataSource;
}

export type AlertSeverity = "info" | "warning" | "critical";

export interface AlertRecord {
  id: string;
  hubId: string;
  deviceId: string;
  createdAt: number;
  severity: AlertSeverity;
  title: string;
  message: string;
  acknowledged: boolean;
  demo: true;
}

export interface HubSnapshot {
  hub: Hub;
  devices: Device[];
  latest: Record<string, Measurement>;
  history: Measurement[];
  alerts: AlertRecord[];
  receivedAt: number;
}
