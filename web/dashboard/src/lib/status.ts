import type { NodeInfo } from "./types";

/**
 * Status byte sent by the node (SensorData.status). The node's LED code uses
 * the same bits, so the dashboard and the hardware always agree.
 */
export const STATUS_GOOD = 1 << 0; // signal within limits  -> green LED
export const STATUS_BAD = 1 << 1; // signal out of limits   -> red LED
export const STATUS_TRIP = 1 << 2; // fault / trip           -> red and green flashing
export const STATUS_BATTERY = 1 << 3; // running on battery

export type NodeState = "good" | "bad" | "trip" | "offline" | "unknown";

export function deriveState(n: NodeInfo, timeoutS: number): NodeState {
  if (n.age > timeoutS) return "offline";
  if (n.st & STATUS_TRIP) return "trip";
  if (n.st & STATUS_BAD) return "bad";
  if (n.st & STATUS_GOOD) return "good";
  return "unknown";
}

export const STATE_LABEL: Record<NodeState, string> = {
  good: "Good signal",
  bad: "Bad signal",
  trip: "Tripped",
  offline: "Offline",
  unknown: "No status",
};

/** What the two indicator LEDs on the node show for each state. */
export const LED_FOR_STATE: Record<NodeState, string> = {
  good: "Green steady",
  bad: "Red steady",
  trip: "Red and green flashing",
  offline: "Both off",
  unknown: "Both off",
};

export const onBattery = (n: NodeInfo) => (n.st & STATUS_BATTERY) !== 0;
