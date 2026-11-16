import * as React from "react";
import * as RechartsPrimitive from "recharts";
import { cn } from "@/lib/utils";

export type ChartConfig = Record<string, { label?: React.ReactNode; color?: string; unit?: string; digits?: number }>;

const ChartContext = React.createContext<{ config: ChartConfig } | null>(null);

export function ChartContainer({
  config,
  className,
  children,
  ...props
}: React.ComponentProps<"div"> & {
  config: ChartConfig;
  children: React.ComponentProps<typeof RechartsPrimitive.ResponsiveContainer>["children"];
}) {
  const style = Object.fromEntries(
    Object.entries(config)
      .filter(([, v]) => v.color)
      .map(([k, v]) => [`--color-${k}`, v.color]),
  ) as React.CSSProperties;
  return (
    <ChartContext.Provider value={{ config }}>
      <div
        style={{ ...style, ...props.style }}
        className={cn(
          "flex justify-center text-xs [&_.recharts-cartesian-axis-tick_text]:fill-muted-foreground [&_.recharts-cartesian-grid_line]:stroke-border [&_.recharts-curve.recharts-tooltip-cursor]:stroke-border [&_.recharts-dot[stroke='#fff']]:stroke-transparent [&_.recharts-layer]:outline-hidden [&_.recharts-surface]:outline-hidden",
          className,
        )}
      >
        <RechartsPrimitive.ResponsiveContainer>{children}</RechartsPrimitive.ResponsiveContainer>
      </div>
    </ChartContext.Provider>
  );
}

export const ChartTooltip = RechartsPrimitive.Tooltip;

interface TooltipPayloadItem {
  dataKey?: string | number;
  value?: number | string | null;
  color?: string;
  name?: string;
}

export function ChartTooltipContent({
  active,
  payload,
  label,
  labelFormatter,
  className,
}: {
  active?: boolean;
  payload?: TooltipPayloadItem[];
  label?: number | string;
  labelFormatter?: (label: number | string) => React.ReactNode;
  className?: string;
}) {
  const ctx = React.useContext(ChartContext);
  if (!active || !payload?.length || !ctx) return null;
  return (
    <div className={cn("grid min-w-32 gap-1.5 rounded-lg border bg-card px-3 py-2 text-xs shadow-lg", className)}>
      {label !== undefined && (
        <div className="font-medium text-foreground">{labelFormatter ? labelFormatter(label) : label}</div>
      )}
      {payload.map((item) => {
        const key = String(item.dataKey);
        const cfg = ctx.config[key];
        const val = typeof item.value === "number" ? item.value.toFixed(cfg?.digits ?? 1) : "-";
        return (
          <div key={key} className="flex items-center gap-2">
            <span className="size-2 rounded-[2px]" style={{ background: `var(--color-${key}, ${item.color})` }} />
            <span className="text-muted-foreground">{cfg?.label ?? key}</span>
            <span className="tnum ml-auto pl-4 font-medium text-foreground">
              {val}
              {cfg?.unit ? ` ${cfg.unit}` : ""}
            </span>
          </div>
        );
      })}
    </div>
  );
}
