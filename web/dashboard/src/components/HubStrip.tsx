import { Card } from "@/components/ui/card";
import { Skeleton } from "@/components/ui/skeleton";
import type { NodesResponse } from "@/lib/types";

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="min-w-0">
      <div className="text-[11px] uppercase tracking-wide text-muted-foreground">{label}</div>
      <div className="tnum truncate text-sm font-medium">{value}</div>
    </div>
  );
}

export function HubStrip({ data }: { data: NodesResponse | null }) {
  if (!data) return <Skeleton className="h-[58px] w-full rounded-xl" />;
  return (
    <Card className="grid grid-cols-2 gap-x-6 gap-y-2 px-5 py-3 sm:grid-cols-4 lg:grid-cols-6">
      <Stat label="Hub" value={data.hubHot ? "Hot, throttled" : "Running"} />
      <Stat label="Poll cycle" value={`#${data.cycle}`} />
      <Stat label="Answered" value={`${data.answered} / ${data.polled} nodes`} />
      <Stat label="Cycle time" value={`${data.ms} ms`} />
      <Stat label="Hub chip" value={`${data.hubChip.toFixed(0)} °C`} />
      <Stat label="Clock" value={data.clock} />
    </Card>
  );
}
