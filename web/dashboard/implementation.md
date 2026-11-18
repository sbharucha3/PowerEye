# PowerEye Implementation Plan

## 1. Product goal

Build **PowerEye**, a mobile-first web application for monitoring
electrical systems through custom-built meters using microcontrollers
(such as ESP32 boards) and electrical sensors.

Users should be able to scan a QR code associated with a PowerEye hub,
open the website on their phone, sign in, and view the hub's devices,
live measurements, alerts, and historical readings.

The existing Figma design and its React/TypeScript prototype (`App.tsx`
and `index.css`, if present in the repository) are the starting point
for the user interface. Preserve the established visual design and
workflows unless a change is necessary and explained.

## 2. Confirmed requirements

### User experience

-   The primary interface is a **mobile-first website** designed and
    optimized for phones.
-   A user should be able to scan a hub's QR code and open its PowerEye
    page in a mobile browser.
-   Consider making the website installable as a Progressive Web App
    (PWA).
-   Multiple users must be able to access and view the same hub and
    electrical system.
-   Users should be able to see current readings, device status, alerts,
    and historical readings.

### Real-time behavior

-   Target measurement updates: **once per second**.
-   Maximum desired end-to-end latency: **5 seconds** from measurement
    generation to display on the phone.
-   Alerts should reach connected users as quickly as possible, ideally
    no slower than normal measurement updates.
-   Background push notifications are desirable but are not required for
    the first website milestone.
-   The system should retain historical readings for future review and
    decision-making. Advanced analytics and large-scale historical
    stress testing are not first-release requirements.

### Hardware

-   The project will build its own meters using microcontrollers such as
    ESP32 boards and electrical sensors.
-   A hub will connect the meters to the application/backend.
-   The exact meter-to-hub protocol, hub-to-internet connection,
    electrical sensor specifications, and telemetry format are **not yet
    confirmed**.
-   Do not assume MQTT, Wi-Fi, Bluetooth, or any other protocol until
    the hardware team confirms it.

### Hub offline behavior

-   Ideally, the hub will buffer readings while offline and upload them
    after reconnecting.
-   This is a later hardware/backend capability, not a requirement for
    the first website milestone.
-   Design data structures so delayed or backfilled readings can be
    stored with their original measurement timestamps.

## 3. Provisional architecture

This is a starting architecture to validate, not a final technology
commitment.

``` text
Custom electrical meters (ESP32 + sensors)
                  |
                  v
             PowerEye hub
                  |
                  v
      Device ingestion / messaging layer
       (protocol to be selected later)
                  |
          +-------+--------+
          |                |
          v                v
   Measurement store   Alert processing
          |                |
          +-------+--------+
                  |
                  v
        Backend / application services
                  |
          Realtime updates to clients
                  |
                  v
     Mobile-first React website on Vercel
```

Potential technology choices: - **Frontend:** React + TypeScript, based
on the existing prototype. - **Website hosting:** Vercel. - **Database
and authentication:** Supabase (PostgreSQL + Supabase Auth). - **Live
updates to open website sessions:** Supabase Realtime or another
persistent realtime transport, selected and load-tested against the
one-second / five-second target. - **Device ingestion:** A separate
service and, if appropriate after hardware review, an MQTT broker or
managed IoT platform. - **Background phone notifications:** PWA service
worker + Web Push, considered after the core website works.

Important distinctions: - Vercel hosts and deploys the website; it does
not need to be the continuously connected listener for physical
meters. - Realtime delivery from the backend to a browser and telemetry
transport from a hub to the backend are separate problems. - Do not
assume that database change subscriptions alone will meet the required
latency and throughput. Measure the full path under realistic simulated
load. - Do not put device secrets or privileged database credentials in
browser code.

## 4. Implementation phases

### Phase 0 --- Inspect and document the existing project

1.  Inspect the repository, Figma exports, `App.tsx`, `index.css`,
    package files, and existing assets.
2.  Identify the current framework, routes/screens, components,
    dependencies, and which interactions are static or functional.
3.  Map the existing screens and flows:
    -   Power Map / home
    -   Add Device
    -   History
    -   Device details and status
    -   Navigation and alerts, if present
4.  Preserve the Figma design. Record gaps and assumptions rather than
    silently redesigning.
5.  Create or update the project documentation and Copilot instructions.

**Deliverable:** A runnable frontend that faithfully reflects the
existing design, with a short inventory of static/demo behavior.

### Phase 1 --- Establish the mobile-first website

1.  Set up the React + TypeScript project (use Vite if the repository
    does not already have a suitable setup).
2.  Configure the repository, scripts, environment-variable conventions,
    linting, and basic tests.
3.  Deploy a basic version to Vercel.
4.  Verify the website works at common phone screen sizes and in mobile
    browsers.
5.  Implement QR-code URL routing for a hub, such as `/hub/{hubId}`. At
    first, this can resolve to a demo hub.
6.  Do not require a native iOS or Android app.

**Deliverable:** A deployed, mobile-friendly website that opens from a
hub-specific URL.

### Phase 2 --- Define the data model and shared access

Define and document the minimum data model before connecting real
hardware. At minimum, consider: - Users - Hubs - Hub membership / access
permissions - Meters or devices - Measurement records - Alerts /
events - Device connectivity status

Every measurement should include, as appropriate: - A stable device
identifier - A measurement timestamp (when the reading was taken) - A
server-received timestamp (when the backend received it) - Measurement
values and units - Data-quality / validity information where available

Support multiple authorized users viewing the same hub. Use server-side
authorization and database row-level security where supported. A QR code
should identify a hub, **not act as an unrestricted access credential**.

**Deliverable:** Documented schema and a working demo of shared hub
access.

### Phase 3 --- Simulated real-time data pipeline

Before hardware integration, build a simulated telemetry source that
produces realistic sample readings at the intended cadence.

1.  Send simulated measurements through the same ingestion path intended
    for hardware.
2.  Persist measurements.
3.  Deliver updates to open browser sessions in real time.
4.  Show connection state and the age of the most recent reading.
5.  Generate sample alerts from defined test conditions.
6.  Record timestamps at generation, backend receipt, and browser
    display where practical.
7.  Measure end-to-end latency and test the five-second maximum target.
8.  Test multiple browser sessions viewing the same hub.

**Deliverable:** A working end-to-end demonstration with simulated data,
one-second target updates, shared views, and visible alerts.

### Phase 4 --- Alert handling

1.  Define alert types and thresholds with the hardware/product team.
2.  Evaluate alert conditions as incoming measurements are processed.
3.  Store alerts with timestamps, hub/device identifiers, severity, and
    status.
4.  Deliver alerts to connected website sessions immediately.
5.  Avoid duplicate alerts during reconnects or repeated readings;
    define deduplication behavior.
6.  Add an in-app alert list/history.
7.  Later, add optional Web Push notifications for users who grant
    permission.

Do not invent electrical safety thresholds. They must come from
validated sensor specifications, engineering analysis, and product
requirements.

**Deliverable:** Reliable in-app alert creation and near-real-time
delivery for the simulated pipeline.

### Phase 5 --- Hardware integration

Only after the hardware interface is documented: 1. Confirm the exact
ESP32 board, sensors, measurement capabilities, calibration approach,
and electrical ratings. 2. Confirm meter-to-hub communication and
hub-to-internet connectivity. 3. Define a versioned telemetry message
format, units, timestamps, device identity, and error handling. 4.
Implement a hardware adapter / ingestion interface without coupling
hardware-specific logic to UI components. 5. Authenticate devices
securely. Do not embed shared privileged secrets in the website. 6.
Integrate one meter first, then expand to multiple meters. 7. Validate
readings against appropriate reference equipment and test latency. 8.
Later, add hub-side offline buffering and backfill using original
measurement timestamps.

**Deliverable:** One physical meter sending validated readings through
the hub and appearing in the website.

### Phase 6 --- Historical data and reliability

1.  Store readings for later review.
2.  Add time-range selection and historical charts/tables.
3.  Define retention and aggregation policies when actual data volume is
    known.
4.  Handle stale data, missing readings, device disconnections,
    reconnects, and out-of-order/backfilled data.
5.  Keep historical timestamps distinct from backend receipt timestamps.
6.  Add monitoring and error logging.

**Deliverable:** Useful history and clear system-health behavior.

### Phase 7 --- PWA and production hardening

1.  Evaluate PWA installation and Home Screen experience.
2.  Add Web Push notifications if still desired, with permission and
    platform limitations clearly handled.
3.  Review authentication, authorization, hub sharing, QR-code access,
    and device credentials.
4.  Test on iOS and Android mobile browsers.
5.  Test realistic device counts and one-second update traffic.
6.  Validate the five-second latency target under expected load.
7.  Add deployment, backup, monitoring, and incident-recovery
    procedures.

**Deliverable:** Production-ready mobile-first PowerEye web experience.

## 5. Non-negotiable engineering principles

-   Preserve the existing Figma design and reuse existing components
    where practical.
-   Keep UI components separate from data access, business logic, and
    hardware communication.
-   Use typed interfaces for hubs, devices, measurements, and alerts.
-   Clearly distinguish simulated data from real hardware data.
-   Never silently fabricate readings or present stale readings as
    current.
-   Show when a device is offline or its latest reading is stale.
-   Store measurement time and server receipt time separately.
-   Enforce authorization on the backend; hiding UI elements is not
    security.
-   Keep secrets out of frontend source code and client-side environment
    variables.
-   Do not commit credentials, API keys, or device secrets.
-   Do not implement mains-voltage hardware wiring based on assumptions.
    Use appropriately rated, isolated measurement hardware and qualified
    electrical supervision; the web application should not encourage
    unsafe electrical work.
-   Make architectural decisions explicit in documentation. If a key
    requirement is unknown, ask or use a clearly marked mock rather than
    inventing a protocol or specification.

## 6. First milestone: what to build now

The first development milestone should be a **mobile-first, deployed
website using simulated telemetry**, not a complete hardware system.

It should: 1. Reuse the Figma-based Power Map, Add Device, and History
UI. 2. Open at a hub-specific URL suitable for a QR code. 3. Display
simulated measurements that update approximately once per second. 4.
Show a visible stale/offline state if updates stop. 5. Show simulated
alerts promptly. 6. Allow multiple browser sessions to view the same
demo hub. 7. Be structured so the simulated data source can later be
replaced by the real device ingestion service.

Do not delay the website waiting for final hardware protocol decisions,
but do not pretend the simulated pipeline is already connected to
physical meters.

### First-milestone implementation decision

The browser demo currently uses `MockTelemetryService` and a typed
`TelemetryService`/`HubSnapshot` boundary. Updates are generated
approximately once per second and shared between tabs in the same browser
origin with `BroadcastChannel`; the rolling snapshot is retained in
`localStorage`. This behavior is intentionally browser-local and does not
claim shared live access across separate devices. Cross-device realtime
delivery, authorization, and durable server-side history require a future
application backend and realtime transport.
