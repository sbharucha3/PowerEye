import { describe, expect, it } from "vitest";
import { evaluateDemoAlerts } from "./mockTelemetryService";
import type { AlertRecord, Measurement } from "./telemetry";

function measurement(deviceId: string, current: number | null, now: number): Measurement {
  return {
    deviceId,
    hubId: "demo-hub",
    measuredAt: now,
    receivedAt: deviceId === "video" ? null : now,
    displayedAt: now,
    voltage: 240,
    current,
    power: 50_000,
    powerFactor: 0.95,
    frequency: 60,
    signalDbm: -60,
    source: "simulated",
  };
}

describe("evaluateDemoAlerts", () => {
  it("does not create duplicate alerts for one sustained condition", () => {
    const now = 1_700_000_000_000;
    const latest = {
      craft: measurement("craft", 29, now),
      a: measurement("a", 20, now),
      video: measurement("video", null, now),
    };
    const first = evaluateDemoAlerts([], latest);
    const second = evaluateDemoAlerts(first, latest);

    expect(first.filter((alert) => alert.deviceId === "craft")).toHaveLength(1);
    expect(second.filter((alert) => alert.deviceId === "craft")).toHaveLength(1);
  });

  it("creates a demo no-data alert for an offline device", () => {
    const now = 1_700_000_000_000;
    const latest = {
      video: measurement("video", null, now),
      craft: measurement("craft", 20, now),
      a: measurement("a", 20, now),
    };
    const alerts = evaluateDemoAlerts([], latest);

    expect(alerts.some((alert: AlertRecord) => alert.deviceId === "video" && alert.demo)).toBe(true);
  });
});
