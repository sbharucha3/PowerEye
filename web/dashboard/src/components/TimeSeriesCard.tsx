import { Area, AreaChart, CartesianGrid, ReferenceArea, ReferenceLine, XAxis, YAxis } from "recharts";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { ChartContainer, ChartTooltip, ChartTooltipContent, type ChartConfig } from "@/components/ui/chart";
import { Skeleton } from "@/components/ui/skeleton";
import { fmtClock } from "@/lib/format";
import type { SeriesPoint } from "@/lib/types";

interface Props {
  title: string;
  description: string;
  dataKey: "v" | "a";
  unit: string;
  colorVar: string;
  data: SeriesPoint[] | undefined;
  loaded: boolean;
  longRange: boolean;
  yDomain: [(min: number) => number, (max: number) => number];
  band?: [number, number]; // acceptable range, drawn as a soft band
  limit?: { y: number; label: string };
}

export function TimeSeriesCard(p: Props) {
  const config: ChartConfig = { [p.dataKey]: { label: p.title, color: `var(${p.colorVar})`, unit: p.unit, digits: 1 } };
  const gradId = `grad-${p.dataKey}`;
  const vals = (p.data ?? []).map((d) => d[p.dataKey]).filter((x): x is number => x != null);
  const hasData = vals.length > 0;
  const lo = p.yDomain[0](hasData ? Math.min(...vals) : 0);
  const hi = p.yDomain[1](hasData ? Math.max(...vals) : 0);
  const step = hi - lo > 40 ? 10 : 5;
  const ticks: number[] = [];
  for (let y = lo; y <= hi + 1e-9; y += step) ticks.push(y);
  return (
    <Card>
      <CardHeader>
        <div>
          <CardTitle>{p.title}</CardTitle>
          <CardDescription>{p.description}</CardDescription>
        </div>
      </CardHeader>
      <CardContent>
        <div className="h-56">
          {!p.loaded && !p.data ? (
            <Skeleton className="h-full w-full" />
          ) : !hasData ? (
            <div className="grid h-full place-items-center rounded-lg border border-dashed text-sm text-muted-foreground">
              No samples in this window yet
            </div>
          ) : (
            <ChartContainer config={config} className="h-full w-full">
              <AreaChart data={p.data} margin={{ top: 8, right: 8, bottom: 0, left: -12 }}>
                <defs>
                  <linearGradient id={gradId} x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" stopColor={`var(--color-${p.dataKey})`} stopOpacity={0.28} />
                    <stop offset="100%" stopColor={`var(--color-${p.dataKey})`} stopOpacity={0.02} />
                  </linearGradient>
                </defs>
                <CartesianGrid vertical={false} strokeDasharray="3 3" />
                <XAxis
                  dataKey="t"
                  type="number"
                  scale="time"
                  domain={["dataMin", "dataMax"]}
                  tickFormatter={(t: number) => fmtClock(t, !p.longRange)}
                  tickLine={false}
                  axisLine={false}
                  minTickGap={48}
                  tickMargin={8}
                />
                <YAxis
                  domain={[lo, hi]}
                  ticks={ticks}
                  tickLine={false}
                  axisLine={false}
                  width={44}
                  tickFormatter={(v: number) => `${Math.round(v)}`}
                />
                {p.band && (
                  <ReferenceArea y1={p.band[0]} y2={p.band[1]} fill="var(--good)" fillOpacity={0.07} ifOverflow="hidden" />
                )}
                {p.limit && (
                  <ReferenceLine
                    y={p.limit.y}
                    stroke="var(--bad)"
                    strokeDasharray="5 4"
                    strokeOpacity={0.7}
                    ifOverflow="hidden"
                    label={{ value: p.limit.label, position: "insideTopRight", fill: "var(--bad)", fontSize: 11 }}
                  />
                )}
                <ChartTooltip
                  cursor={{ strokeDasharray: "3 3" }}
                  content={<ChartTooltipContent labelFormatter={(t) => fmtClock(Number(t))} />}
                />
                <Area
                  dataKey={p.dataKey}
                  type="monotone"
                  stroke={`var(--color-${p.dataKey})`}
                  strokeWidth={2}
                  fill={`url(#${gradId})`}
                  connectNulls={false}
                  dot={false}
                  isAnimationActive={false}
                />
              </AreaChart>
            </ChartContainer>
          )}
        </div>
      </CardContent>
    </Card>
  );
}
