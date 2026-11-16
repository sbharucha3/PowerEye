import { Moon, Sun } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";
import { fmtClock } from "@/lib/format";

interface Props {
  connecting: boolean;
  usingDemo: boolean;
  forcedDemo: boolean;
  hubUnreachable: boolean;
  lastUpdate: number | null;
  dark: boolean;
  onToggleTheme: () => void;
  onToggleDemo: () => void;
}

export function Header(p: Props) {
  const reason = p.forcedDemo
    ? "Demo mode is on. Values are simulated."
    : "The hub did not answer 3 times in a row. Showing simulated data and retrying in the background.";
  return (
    <header className="sticky top-0 z-30 border-b bg-background/85 backdrop-blur">
      <div className="mx-auto flex h-16 max-w-6xl items-center justify-between gap-4 px-4 sm:px-6">
        <div className="flex items-center gap-3">
          <div className="relative grid size-9 place-items-center rounded-lg bg-primary text-primary-foreground">
            <span className="size-3.5 rounded-full bg-good ring-2 ring-primary-foreground/30" />
          </div>
          <div className="leading-tight">
            <h1 className="text-base font-semibold tracking-tight">PowerEye</h1>
            <p className="hidden text-xs text-muted-foreground sm:block">Temporary power monitor</p>
          </div>
        </div>

        <div className="flex items-center gap-2 sm:gap-3">
          {p.connecting ? (
            <Badge variant="muted">
              <span className="pe-pulse size-1.5 rounded-full bg-muted-foreground" />
              Connecting
            </Badge>
          ) : p.usingDemo ? (
            <Tooltip>
              <TooltipTrigger asChild>
                <span>
                  <Badge variant="warn">
                    <span className="size-1.5 rounded-full bg-warn" />
                    Demo data
                  </Badge>
                </span>
              </TooltipTrigger>
              <TooltipContent>{reason}</TooltipContent>
            </Tooltip>
          ) : (
            <Badge variant="good">
              <span className="pe-pulse size-1.5 rounded-full bg-good" />
              Hub connected
            </Badge>
          )}
          <span className="tnum hidden text-xs text-muted-foreground md:inline">
            {p.lastUpdate ? `Updated ${fmtClock(p.lastUpdate)}` : "Waiting for data"}
          </span>
          <Button variant="outline" size="sm" onClick={p.onToggleDemo}>
            {p.forcedDemo ? "Use live hub" : p.hubUnreachable ? "Retry live" : "Demo data"}
          </Button>
          <Button variant="ghost" size="icon" onClick={p.onToggleTheme} aria-label="Toggle theme">
            {p.dark ? <Sun /> : <Moon />}
          </Button>
        </div>
      </div>
    </header>
  );
}
