import { CartesianGrid, Line, LineChart, XAxis, YAxis } from "recharts";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { ChartContainer, ChartTooltip, ChartTooltipContent, type ChartConfig } from "@/components/ui/chart";
import { fmtClock } from "@/lib/format";
import type { Snapshot } from "@/lib/types";

const config: ChartConfig = {
  rssi: { label: "RSSI", color: "var(--chart-3)", unit: "dBm", digits: 0 },
  snr: { label: "SNR", color: "var(--chart-4)", unit: "dB", digits: 1 },
};

export function LinkChart({ data, windowMs }: { data: Snapshot[] | undefined; windowMs: number }) {
  const cutoff = Date.now() - windowMs;
  const points = (data ?? []).filter((s) => s.t >= cutoff && s.online);
  return (
    <Card className="h-full">
      <CardHeader>
        <div>
          <CardTitle>Radio link quality</CardTitle>
          <CardDescription>How well the hub hears the node. Recorded since this page was opened.</CardDescription>
        </div>
      </CardHeader>
      <CardContent>
        <div className="h-56">
          {points.length < 2 ? (
            <div className="grid h-full place-items-center rounded-lg border border-dashed text-sm text-muted-foreground">
              Collecting link samples
            </div>
          ) : (
            <ChartContainer config={config} className="h-full w-full">
              <LineChart data={points} margin={{ top: 8, right: 4, bottom: 0, left: -12 }}>
                <CartesianGrid vertical={false} strokeDasharray="3 3" />
                <XAxis
                  dataKey="t"
                  type="number"
                  scale="time"
                  domain={["dataMin", "dataMax"]}
                  tickFormatter={(t: number) => fmtClock(t)}
                  tickLine={false}
                  axisLine={false}
                  minTickGap={56}
                  tickMargin={8}
                />
                <YAxis yAxisId="rssi" domain={["dataMin - 6", "dataMax + 6"]} tickFormatter={(v: number) => `${Math.round(v)}`} tickLine={false} axisLine={false} width={44} />
                <YAxis yAxisId="snr" orientation="right" domain={["dataMin - 3", "dataMax + 3"]} tickFormatter={(v: number) => v.toFixed(0)} tickLine={false} axisLine={false} width={32} />
                <ChartTooltip content={<ChartTooltipContent labelFormatter={(t) => fmtClock(Number(t))} />} />
                <Line yAxisId="rssi" dataKey="rssi" type="monotone" stroke="var(--color-rssi)" strokeWidth={2} dot={false} isAnimationActive={false} />
                <Line yAxisId="snr" dataKey="snr" type="monotone" stroke="var(--color-snr)" strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ChartContainer>
          )}
        </div>
        <div className="mt-2 flex items-center gap-4 text-xs text-muted-foreground">
          <span className="flex items-center gap-1.5">
            <span className="h-0.5 w-3 rounded bg-chart-3" /> RSSI (left, dBm)
          </span>
          <span className="flex items-center gap-1.5">
            <span className="h-0.5 w-3 rounded bg-chart-4" /> SNR (right, dB)
          </span>
        </div>
      </CardContent>
    </Card>
  );
}
