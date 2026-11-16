import { BatteryMedium, Radio, Thermometer } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { fmt, fmtDuration } from "@/lib/format";
import { identifyNode } from "@/lib/api";
import { deriveState, LED_FOR_STATE, onBattery, STATE_LABEL, type NodeState } from "@/lib/status";
import type { NodeInfo } from "@/lib/types";
import { cn } from "@/lib/utils";

const PILL: Record<NodeState, "good" | "bad" | "warn" | "muted"> = {
  good: "good",
  bad: "warn",
  trip: "bad",
  offline: "muted",
  unknown: "muted",
};

function Led({ state }: { state: NodeState }) {
  return (
    <span
      aria-hidden
      className={cn(
        "inline-block size-3 rounded-full",
        state === "good" && "bg-good shadow-[0_0_0_4px_color-mix(in_oklch,var(--good)_25%,transparent)]",
        state === "bad" && "bg-bad shadow-[0_0_0_4px_color-mix(in_oklch,var(--bad)_25%,transparent)]",
        state === "trip" && "pe-led-trip",
        (state === "offline" || state === "unknown") && "bg-muted-foreground/30",
      )}
    />
  );
}

function Readout({ label, value, unit, dim }: { label: string; value: string; unit: string; dim: boolean }) {
  return (
    <div className={cn("min-w-0", dim && "opacity-45")}>
      <div className="text-xs text-muted-foreground">{label}</div>
      <div className="flex items-baseline gap-1.5">
        <span className="tnum text-4xl font-semibold tracking-tight sm:text-5xl">{value}</span>
        <span className="text-base text-muted-foreground">{unit}</span>
      </div>
    </div>
  );
}

function Metric({ label, value, hint }: { label: string; value: string; hint?: string }) {
  return (
    <div className="min-w-0">
      <div className="text-[11px] uppercase tracking-wide text-muted-foreground">{label}</div>
      <div className="tnum truncate text-sm font-medium">{value}</div>
      {hint && <div className="truncate text-[11px] text-muted-foreground">{hint}</div>}
    </div>
  );
}

export function NodeCard({
  node,
  timeoutS,
  sampleMs,
  live,
}: {
  node: NodeInfo;
  timeoutS: number;
  sampleMs: number;
  live: boolean;
}) {
  const state = deriveState(node, timeoutS);
  const offline = state === "offline";
  const link = node.polls ? (100 * node.replies) / node.polls : 0;
  const delivered = node.rx + node.lost ? (100 * node.rx) / (node.rx + node.lost) : 0;
  const uptime = ((node.rx + node.lost) * sampleMs) / 1000;
  const power = node.w >= 1000 ? { v: (node.w / 1000).toFixed(2), u: "kW" } : { v: node.w.toFixed(0), u: "W" };

  return (
    <Card>
      <CardHeader className="items-center">
        <div className="flex flex-wrap items-center gap-2.5">
          <CardTitle className="text-base">Node {node.id}</CardTitle>
          <Badge variant={PILL[state]}>{STATE_LABEL[state]}</Badge>
          {onBattery(node) && (
            <Badge variant="warn">
              <BatteryMedium className="size-3.5" />
              On battery
            </Badge>
          )}
        </div>
        <div className="flex items-center gap-3">
          <div className="hidden items-center gap-2 text-xs text-muted-foreground sm:flex">
            <Led state={state} />
            LED: {LED_FOR_STATE[state]}
          </div>
          {live && (
            <Button variant="outline" size="sm" onClick={() => void identifyNode(node.id)}>
              Identify
            </Button>
          )}
        </div>
      </CardHeader>
      <CardContent className="space-y-5">
        <div className="grid grid-cols-1 gap-5 sm:grid-cols-3">
          <Readout label="Voltage" value={fmt(node.v, 1)} unit="V" dim={offline} />
          <Readout label="Current" value={fmt(node.a, 1)} unit="A" dim={offline} />
          <Readout label="Power" value={power.v} unit={power.u} dim={offline} />
        </div>
        {offline && (
          <p className="text-xs text-muted-foreground">
            No reply for {fmtDuration(node.age)}. Showing the last reading received.
          </p>
        )}
        <div className="grid grid-cols-2 gap-x-6 gap-y-4 border-t pt-4 sm:grid-cols-4">
          <Metric label="Uptime (est.)" value={fmtDuration(uptime)} hint={`${node.buffered} samples buffered`} />
          <Metric label="Link" value={`${fmt(link, 1)} %`} hint={`${node.replies} of ${node.polls} polls`} />
          <Metric label="Data delivered" value={`${fmt(delivered, 1)} %`} hint={`${node.rx} rx, ${node.lost} lost`} />
          <Metric label="Last heard" value={`${fmt(node.age, 0)} s ago`} hint={`${node.backfilled} backfilled`} />
          <Metric
            label="Hub hears node"
            value={`${fmt(node.rssi, 0)} dBm`}
            hint={`SNR ${fmt(node.snr, 1)} dB`}
          />
          <Metric
            label="Node hears hub"
            value={`${fmt(node.nrssi, 0)} dBm`}
            hint={`SNR ${fmt(node.nsnr, 1)} dB`}
          />
          <Metric label="Node chip" value={`${fmt(node.chip, 0)} °C`} hint={`${node.resets} radio resets`} />
          <div className="flex min-w-0 items-end gap-3 text-muted-foreground">
            <Radio className="size-4 shrink-0" />
            <Thermometer className="size-4 shrink-0" />
            <span className="truncate text-[11px]">915 MHz, SF7</span>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}

export function NodeCardSkeleton() {
  return <div className="h-[330px] animate-pulse rounded-xl border bg-muted/50 sm:h-[300px]" />;
}
