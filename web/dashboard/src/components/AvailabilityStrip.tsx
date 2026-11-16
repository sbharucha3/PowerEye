import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Skeleton } from "@/components/ui/skeleton";
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";
import { fmtClock } from "@/lib/format";
import type { SeriesPoint } from "@/lib/types";

const SEGMENTS = 72;

export function AvailabilityStrip({
  data,
  loaded,
  rangeLabel,
}: {
  data: SeriesPoint[] | undefined;
  loaded: boolean;
  rangeLabel: string;
}) {
  const pts = data ?? [];
  const t0 = pts.length ? pts[0].t : 0;
  const t1 = pts.length ? pts[pts.length - 1].t : 0;
  const span = Math.max(1, t1 - t0);
  const buckets: { up: boolean; any: boolean; start: number }[] = Array.from({ length: SEGMENTS }, (_, i) => ({
    up: false,
    any: false,
    start: t0 + (span * i) / SEGMENTS,
  }));
  for (const p of pts) {
    const i = Math.min(SEGMENTS - 1, Math.floor(((p.t - t0) / span) * SEGMENTS));
    buckets[i].any = true;
    if (p.v != null) buckets[i].up = true;
  }
  const filled = buckets.filter((b) => b.any);
  const pct = filled.length ? (100 * filled.filter((b) => b.up).length) / filled.length : 0;
  return (
    <Card>
      <CardHeader>
        <div>
          <CardTitle>Signal uptime</CardTitle>
          <CardDescription>Samples received from the node over the last {rangeLabel}.</CardDescription>
        </div>
        <div className="tnum text-right">
          <div className="text-2xl font-semibold tracking-tight">{filled.length ? `${pct.toFixed(1)}%` : "-"}</div>
          <div className="text-[11px] uppercase tracking-wide text-muted-foreground">available</div>
        </div>
      </CardHeader>
      <CardContent>
        {!loaded && !data ? (
          <Skeleton className="h-8 w-full" />
        ) : !pts.length ? (
          <div className="grid h-8 place-items-center rounded-md border border-dashed text-xs text-muted-foreground">
            No history yet
          </div>
        ) : (
          <>
            <div className="flex h-8 gap-[2px]">
              {buckets.map((b, i) => (
                <Tooltip key={i}>
                  <TooltipTrigger asChild>
                    <div
                      className={
                        "flex-1 rounded-[3px] " + (!b.any ? "bg-muted" : b.up ? "bg-good/80" : "bg-bad/75")
                      }
                    />
                  </TooltipTrigger>
                  <TooltipContent>
                    {fmtClock(b.start)} {!b.any ? "no data" : b.up ? "receiving" : "no samples"}
                  </TooltipContent>
                </Tooltip>
              ))}
            </div>
            <div className="tnum mt-1.5 flex justify-between text-[11px] text-muted-foreground">
              <span>{fmtClock(t0, false)}</span>
              <span>{fmtClock(t1, false)}</span>
            </div>
          </>
        )}
      </CardContent>
    </Card>
  );
}
