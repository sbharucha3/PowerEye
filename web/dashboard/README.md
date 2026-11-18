# PowerEye dashboard

Mobile-first React and TypeScript website for the PowerEye hub experience. It preserves the Figma Power Map, Add Device, and History screens.

## Local setup

```bash
cd web/dashboard
npm install
npm run dev
```

Open:

- `http://localhost:5173/hub/demo-hub`
- `http://localhost:5173/hub/<any-hub-id>` for another clearly labeled demo hub URL

The app redirects `/` to the configured demo hub.

## Demo telemetry

The app uses `MockTelemetryService` in `src/lib/mockTelemetryService.ts`. It:

- generates a new typed sample approximately once per second;
- marks every reading as `source: "simulated"`;
- keeps a modest rolling in-browser history;
- provides demo-only alerts for a high-current and high-load marker;
- keeps a sustained alert open rather than repeatedly creating duplicate alerts;
- shows a stale/offline state if the receiving browser has not seen an update for more than five seconds.

Demo thresholds are not validated electrical safety limits.

### Shared sessions

The first milestone has no backend. `BroadcastChannel` shares snapshot updates between tabs in the same browser origin/profile. `localStorage` retains the demo snapshot and history for that browser. It does **not** share live data across separate phones, computers, or browsers. A Supabase or application backend is required for shared multi-user production sessions.

The QR code identifies a hub route; it is not authorization. The demo route is intentionally public, and no real-hub access policy is implied.

## Hub QR URLs

A QR code can encode:

```text
https://your-vercel-domain.example/hub/demo-hub
```

Any QR library can generate this link. Keep the deployed host HTTPS. Production authorization must be enforced by the future backend; QR possession alone must never grant private-hub access.

## Environment variables

Copy `.env.example` to `.env.local`:

```dotenv
VITE_DEMO_HUB_ID=demo-hub
```

Only `VITE_*` values should be browser-visible. Never put service-role keys or device secrets in browser variables.

## Commands

```bash
npm run dev
npm run test
npm run build
npm run preview
```

## Vercel deployment

1. Import the repository in Vercel.
2. Set **Root Directory** to `web/dashboard`.
3. Add `VITE_DEMO_HUB_ID=demo-hub` if you want a hub ID different from the default.
4. Deploy. `vercel.json` rewrites hub URLs to the Vite SPA entry point.
5. Open `/hub/demo-hub` and test after deployment.

## Architecture boundary

- React components render and accept a typed `HubSnapshot`.
- `TelemetryService` is an abstraction for current measurements, history, and alerts.
- `MockTelemetryService` is browser-only demo transport.
- A future hardware ingestion service should receive device messages independently.
- A future application backend should own authentication, hub authorization, persistence, and realtime browser transport.
- Vercel should host the frontend, not continuously listen to long-lived meter or MQTT connections.
