# PowerEye dashboard (demo)

Localhost dashboard for the LoRa hub: live voltage, current and power, node status
(mirrors the node's LEDs), trends, signal uptime, radio link quality and an event log.
Vite + React + TypeScript + Tailwind v4 + shadcn/ui (Recharts).

## Run

```bash
cd web/dashboard
bun install        # or: npm install
bun run dev        # or: npm run dev   ->  http://localhost:5173
```

### With real hardware
1. Power the hub and a node.
2. Join the hub's Wi-Fi network (`LoRa-Hub`; the password is in `src/lora_star/config.h`).
3. Start the dev server. It proxies `/api/*` to `http://192.168.4.1` (the hub), so there are no CORS problems.
   Different hub address: `HUB_URL=http://host bun run dev`.

If the hub does not answer 3 times in a row the page switches to simulated data and shows a
**Demo data** badge. It keeps retrying the hub and returns to live data when it answers.

### Demo without hardware
```bash
VITE_MOCK=1 bun run dev
```
Or press **Demo data** in the header. The mock follows a 10 minute script: normal, a voltage sag
(bad signal), an overcurrent (trip), a dropout (offline) and a battery period.

## Data the hub provides (`src/lora_star/hub.ino`)
- `GET /api/nodes`: hub stats plus `nodes[]` (`v`, `a`, `w`, `st`, `rssi`, `snr`, `polls`, `replies`, `rx`, `lost`, ...). Polled every 2.5 s.
- `GET /api/history?m=v|a&min=N`: bucketed series per node. Refreshed every 6 s.
- `GET /api/time?ms=`: sets the hub clock once per boot (the dashboard does this on first contact).
- `GET /api/cmd?id=N&c=identify`: used by the **Identify** button.

## Status byte (`st`), shared with the node's LED code
Defined in `src/lib/status.ts`.

| Bit | Meaning | LED |
|---|---|---|
| 0 | Good signal | green |
| 1 | Bad signal (out of limits) | red |
| 2 | Trip / fault | red and green flashing |
| 3 | On battery | (badge) |
| none set, or node silent | Offline | both off |

## Notes
- Values are the node's **line-equivalent** readings (for example 240 mVrms injected = 120 V, 30 mVrms = 15 A).
- Uptime is estimated from the number of samples the node has taken (`rx + lost`) times the sample period.
- The link chart and event log only cover the time this page has been open. Voltage, current and the uptime strip come from the hub's own history.
