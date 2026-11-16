import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { fmtClock } from "@/lib/format";
import type { HubEvent } from "@/hooks/useHub";
import { cn } from "@/lib/utils";

const DOT: Record<HubEvent["state"], string> = {
  seen: "bg-chart-1",
  good: "bg-good",
  bad: "bg-warn",
  trip: "bg-bad",
  offline: "bg-muted-foreground",
  unknown: "bg-muted-foreground",
};

export function EventLog({ events }: { events: HubEvent[] }) {
  return (
    <Card className="h-full">
      <CardHeader>
        <div>
          <CardTitle>Event log</CardTitle>
          <CardDescription>State changes seen while this page is open.</CardDescription>
        </div>
      </CardHeader>
      <CardContent>
        <div className="h-56 overflow-y-auto pr-1">
          {events.length === 0 ? (
            <div className="grid h-full place-items-center rounded-lg border border-dashed px-4 text-center text-sm text-muted-foreground">
              No events yet. Going offline, tripping or recovering will show up here.
            </div>
          ) : (
            <ol className="space-y-3">
              {events.map((e, i) => (
                <li key={`${e.t}-${i}`} className="flex items-start gap-3 text-sm">
                  <span className={cn("mt-1.5 size-2 shrink-0 rounded-full", DOT[e.state])} />
                  <div className="min-w-0">
                    <div className="leading-snug">{e.text}</div>
                    <div className="tnum text-xs text-muted-foreground">{fmtClock(e.t)}</div>
                  </div>
                </li>
              ))}
            </ol>
          )}
        </div>
      </CardContent>
    </Card>
  );
}
