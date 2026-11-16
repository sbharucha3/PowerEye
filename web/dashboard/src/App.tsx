import { useState } from "react";
import { AvailabilityStrip } from "@/components/AvailabilityStrip";
import { EventLog } from "@/components/EventLog";
import { Header } from "@/components/Header";
import { HubStrip } from "@/components/HubStrip";
import { LinkChart } from "@/components/LinkChart";
import { NodeCard, NodeCardSkeleton } from "@/components/NodeCard";
import { TimeSeriesCard } from "@/components/TimeSeriesCard";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { TooltipProvider } from "@/components/ui/tooltip";
import { useHub } from "@/hooks/useHub";
import { useTheme } from "@/hooks/useTheme";
import { RANGE_MINUTES, type RangeKey } from "@/lib/types";

const RANGE_LABEL: Record<RangeKey, string> = { "5m": "5 minutes", "1h": "hour", "8h": "8 hours" };

export default function App() {
  const [range, setRange] = useState<RangeKey>("5m");
  const [selected, setSelected] = useState<number | null>(null);
  const theme = useTheme();
  const hub = useHub(range);

  const nodes = hub.nodes?.nodes ?? [];
  const activeId = selected && nodes.some((n) => n.id === selected) ? selected : (nodes[0]?.id ?? null);
  const series = activeId != null ? hub.series[activeId] : undefined;
  const snaps = activeId != null ? hub.snapshots[activeId] : undefined;
  const windowMs = RANGE_MINUTES[range] * 60000;

  return (
    <TooltipProvider delayDuration={150}>
      <Header
        connecting={hub.connecting}
        usingDemo={hub.usingDemo}
        forcedDemo={hub.forcedDemo}
        hubUnreachable={hub.hubUnreachable}
        lastUpdate={hub.lastUpdate}
        dark={theme.dark}
        onToggleTheme={theme.toggle}
        onToggleDemo={hub.toggleDemo}
      />
      <main className="mx-auto max-w-6xl space-y-6 px-4 py-6 sm:px-6">
        <HubStrip data={hub.nodes} />

        <section className="space-y-4" aria-label="Live readings">
          {hub.connecting ? (
            <NodeCardSkeleton />
          ) : nodes.length === 0 ? (
            <div className="grid h-[200px] place-items-center rounded-xl border border-dashed px-6 text-center">
              <div>
                <p className="font-medium">No sensor nodes heard yet</p>
                <p className="mt-1 text-sm text-muted-foreground">
                  Power on a node. The hub probes new node IDs every cycle and the node appears here automatically.
                </p>
              </div>
            </div>
          ) : (
            nodes.map((n) => (
              <NodeCard
                key={n.id}
                node={n}
                timeoutS={hub.nodes!.timeout}
                sampleMs={hub.nodes!.sample}
                live={!hub.usingDemo}
              />
            ))
          )}
        </section>

        <section className="space-y-4" aria-label="Trends">
          <div className="flex flex-wrap items-center justify-between gap-3">
            <div className="flex items-center gap-3">
              <h2 className="text-sm font-semibold tracking-tight">Trends</h2>
              {nodes.length > 1 && (
                <Tabs value={String(activeId)} onValueChange={(v) => setSelected(Number(v))}>
                  <TabsList>
                    {nodes.map((n) => (
                      <TabsTrigger key={n.id} value={String(n.id)}>
                        Node {n.id}
                      </TabsTrigger>
                    ))}
                  </TabsList>
                </Tabs>
              )}
            </div>
            <Tabs value={range} onValueChange={(v) => setRange(v as RangeKey)}>
              <TabsList>
                <TabsTrigger value="5m">5 min</TabsTrigger>
                <TabsTrigger value="1h">1 hour</TabsTrigger>
                <TabsTrigger value="8h">8 hours</TabsTrigger>
              </TabsList>
            </Tabs>
          </div>

          <div className="grid gap-4 lg:grid-cols-2">
            <TimeSeriesCard
              title="Voltage"
              description="Line voltage. Shaded band is nominal 120 V ±5%."
              dataKey="v"
              unit="V"
              colorVar="--chart-1"
              data={series}
              loaded={hub.historyLoaded}
              longRange={range !== "5m"}
              yDomain={[(m) => Math.floor((Math.min(m, 114) - 3) / 5) * 5, (m) => Math.ceil((Math.max(m, 126) + 3) / 5) * 5]}
              band={[114, 126]}
            />
            <TimeSeriesCard
              title="Current"
              description="Load current. Dashed line is the 15 A design rating."
              dataKey="a"
              unit="A"
              colorVar="--chart-2"
              data={series}
              loaded={hub.historyLoaded}
              longRange={range !== "5m"}
              yDomain={[() => 0, (m) => Math.ceil((Math.max(m, 15) + 1) / 5) * 5]}
              limit={{ y: 15, label: "15 A rating" }}
            />
          </div>

          <AvailabilityStrip data={series} loaded={hub.historyLoaded} rangeLabel={RANGE_LABEL[range]} />

          <div className="grid gap-4 lg:grid-cols-3">
            <div className="lg:col-span-2">
              <LinkChart data={snaps} windowMs={windowMs} />
            </div>
            <EventLog events={hub.events} />
          </div>
        </section>

        <footer className="pb-6 pt-2 text-center text-xs text-muted-foreground">
          PowerEye prototype dashboard. Values are line-equivalent readings from the node's low-voltage test inputs.
        </footer>
      </main>
    </TooltipProvider>
  );
}
