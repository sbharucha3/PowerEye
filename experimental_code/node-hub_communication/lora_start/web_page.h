# LoRa Star Network: XIAO ESP32-S3 + Wio-SX1262

A hub-and-nodes LoRa sensor network. One **hub** polls up to **50 nodes** every **5 s**. Each node samples its sensors every **3 s** on a clock shared across the whole network, so readings from different nodes line up to the same timestamp. A phone or iPad connected to the hub's Wi-Fi shows live data and history, saves every sample, and exports CSV.

Every board runs the **same firmware**. Whether a board is the hub or a node, and which node ID it has, is set over Serial and saved in flash.

> **Status:** written but **not yet compiled or tested on hardware**. Expect to fix a few compile errors on the first build. Flash **one hub + one node** first, then scale up.

---

## Hardware

| Part | Notes |
|---|---|
| Seeed Studio XIAO ESP32-S3 | One per device. Has 8 MB PSRAM and 8 MB flash. |
| Seeed Wio-SX1262 for XIAO (B2B connector kit) | LoRa radio. Pin defines assume the **kit (B2B connector) version**. If you have the pin-header version, change the `LORA_*` pins in `config.h`. |
| 915 MHz LoRa antenna | Goes on the **Wio-SX1262's** u.FL connector. **Never transmit without an antenna attached.** |
| 2.4 GHz Wi-Fi antenna | **Hub only.** Goes on the **XIAO's** u.FL connector. Nodes don't use Wi-Fi. |

**Antennas:** don't swap the LoRa and Wi-Fi antennas, which costs 10–20 dB. Point every LoRa antenna the same way (vertical is best), keep them clear of metal, batteries and USB cables, and mount them as high as practical. A better, higher antenna on the hub improves every link at once.

---

## Software setup (Arduino IDE 2.x)

1. **Board package:** in Settings, add this to *Additional boards manager URLs*:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   Then open Boards Manager and install **"esp32 by Espressif Systems"** (3.x recommended).
2. **Library:** in Library Manager, install **RadioLib** (by Jan Gromeš).
3. **Board settings** (Tools menu):
   - Board: **XIAO_ESP32S3**
   - USB CDC On Boot: **Enabled**
   - PSRAM: **OPI PSRAM** (required for 8 h of hub history; without it the hub keeps only ~5 min)
   - Port: `/dev/cu.usbmodem…` (Mac) or `COMx` (Windows)
4. **Open `lora_star/lora_star.ino`.** The folder **must** be named `lora_star`. The other files open as tabs.
5. **Upload.** If the upload hangs or no port shows: hold **B (BOOT)**, plug in USB (or tap **R (RESET)**), release, then upload again and press **R** afterwards.

> Every board must be flashed with the **same `config.h`**. If you change radio, timing or packet settings, reflash **all** boards.

---

## First-time configuration

Open Serial Monitor at **115200 baud**, line ending **Newline**.

**Hub (one board):**
```
role hub
```
**Each node:**
```
role node
id 1        <- unique per node, 1-50
```
Settings are saved in flash and survive reboots. `show` prints the current config and `help` lists commands.

| Where | Command | What it does |
|---|---|---|
| Any | `role hub` / `role node` | Set the role (board reboots) |
| Any | `id <1-50>` | Set the node ID |
| Any | `show`, `help`, `reboot` | Show config, list commands, reboot |
| Hub | `table` | Table of every node |
| Hub | `csv` | Toggle streaming every sample as CSV |
| Hub | `cmd <id\|all> identify` | Blink a node's LED for 5 s |
| Hub | `cmd <id\|all> reboot` | Reboot a node remotely |
| Node | `stats` | Link and buffer stats |
| Node | `v` | Toggle verbose logging |
| Node | `dump` | Print the node's stored history (up to 1 h) as CSV |

---

## Using the web page (phone / iPad / laptop)

1. Join the Wi-Fi network **`LoRa-Hub`** (password **`lorahub123`**, changeable in `config.h`).
2. Open **http://192.168.4.1** in the browser.
3. Opening the page **sets the hub's clock** from your device. Do this once after each hub boot, or set `STA_SSID`/`STA_PASS` so the hub gets the time from the internet (NTP) instead. eduroam is not supported.

The page shows:
- **Live table:** each node's values, signal quality in both directions, link %, data %, backlog, and chip temperature, plus an Identify button.
- **Saved on this device:** the page saves every sample into the browser's local storage (IndexedDB) on that phone or iPad. Export as CSV (last 1 h / 24 h / 7 days / everything).
- **History chart:** all nodes on one time axis, up to 24 h. Hover or drag to see every node's value at the same moment.

**Keeping data safe on iPhone/iPad:**
- **Add the page to your Home Screen** (Share → Add to Home Screen). Safari can otherwise delete site data after about 7 days without a visit.
- **Settings → Display → Auto-Lock → Never** if you want live saving with no gaps.
- If the page closes, it catches up on reopening from the hub's 8 h buffer.
- Turn off Auto-Join for other nearby Wi-Fi networks, because iOS may leave a network that has no internet.
- **Export CSV regularly.** The browser storage is not a backup.
- Expect roughly 30–40 MB of storage per day for 50 nodes at 3 s.

**CSV format** (one row per node per timestamp; pivot on `epoch_ms` to put nodes side by side):
```
time_utc,epoch_ms,node,voltage_V,current_A,power_W,temp_C,status
```

---

## How it works

```
Every 5 s cycle:
 hub -> POLL(node 1, network clock, "samples I already have")  -> node 1 -> REPLY(newest + missing samples)
 hub -> POLL(node 2, ...)                                       -> node 2 -> REPLY(...)
 ...  then retries for nodes that didn't answer, while time remains
```

- **Polling, no collisions.** Only one radio transmits at a time. A node only speaks when polled. Missed nodes are retried later in the same cycle (up to 3 tries).
- **Discovery.** Each cycle the hub also tries 4 unused IDs, so new nodes join automatically.
- **Shared clock.** Every poll carries the hub's clock, and nodes sync to within a few ms. All nodes sample on the same 3 s grid, so timestamps match exactly across nodes.
- **Node logging and backfill.** Each node keeps 1 h of samples in RAM. Each poll tells the node what the hub already has, and the reply carries the newest sample plus the oldest missing ones. Gaps of up to ~3 min (64 samples) are recovered automatically, and the full hour is available via `dump`.
- **Hub history.** About 8 h for all nodes in PSRAM, served to the phone and exportable at `/history.csv`.
- **Self-recovery.**
  - A watchdog reboots the board if the code hangs for 10 s.
  - The radio is reset after repeated TX errors, after 6 cycles with no replies (hub), or after 60 s without being polled (node).
  - The hub restarts its Wi-Fi if it drops, and lowers Wi-Fi power if its chip goes over 75 °C.
  - On boot, each board prints why it last reset (watchdog, brownout or crash).

**Radio:** 915 MHz, SF7, 500 kHz BW, CR 4/5, 22 dBm. Packets take about 15–28 ms on air. Polling 50 nodes takes about 2 s of each 5 s cycle, and the rest is for retries. Both the hub and the nodes print the real timing at boot and warn if the network won't fit.

---

## Files

| File | Contents |
|---|---|
| `config.h` | **All tunable settings:** pins, radio, timing, buffer sizes, Wi-Fi, power, recovery |
| `protocol.h` | Packet formats and data structures (`SensorData` is where sensor fields live) |
| `lora_star.ino` | Shared setup and loop, radio helpers, Serial commands, watchdog |
| `hub.ino` | Polling state machine, backfill tracking, clock, history, web API, Wi-Fi |
| `node.ino` | Sampling, ring buffer, poll replies, **`readSensors()`** |
| `web_page.h` | The web page (HTML/JS, self-contained, no internet needed) |

**Hub web API:** `/api/nodes` (live), `/api/sync?from=<slot>` (phone sync), `/api/history?m=w&min=60` (chart data), `/history.csv?min=480`, `/api/time?ms=<epoch>`, `/api/cmd?id=<n|all>&c=identify|reboot`.

---

## Key settings (`config.h`)

| Setting | Default | Trade-off |
|---|---|---|
| `SF` | 7 | +1 gives about +2.5 dB range but 2× airtime, so about half as many nodes per cycle |
| `BW_KHZ` | 500 | Lower gives more range but more airtime (125 kHz runs into US channel-hopping rules) |
| `TX_POWER` | 22 dBm | Max. Lower saves power. |
| `MAX_NODES` | 50 | More nodes need more of each cycle |
| `CYCLE_MS` | 5000 | Longer leaves room for more nodes or slower radio settings |
| `SAMPLE_MS` | 3000 | Faster sampling needs a higher `MAX_PER_REPLY` (the build checks this) |
| `MAX_TRIES` | 3 | Retries per node per cycle |
| `BUFFER_SAMPLES` | 1200 | Node history (1 h at 3 s) |
| `HISTORY_SLOTS` | 9600 | Hub history (8 h at 3 s, ~5.8 MB PSRAM) |
| `HUB_WIFI_TX_POWER` | 11 dBm | Raise to `WIFI_POWER_15dBm` if the page drops out |
| `HUB_CPU_MHZ` / `NODE_CPU_MHZ` | 160 / 80 | The ESP32-S3 only supports 240, 160 or 80 |
| `REPLY_TIMEOUT_MARGIN_MS` | 10 | Extra wait for a reply beyond its airtime |

---

## Reading the metrics

| Metric | Meaning | Healthy |
|---|---|---|
| RSSI (dBm) | Signal strength | Above −105 (limit is about −117 at SF7/500 kHz). −60 at a few hundred m in line of sight is normal. |
| SNR (dB) | Signal above noise | Above +5. Below −7 starts losing packets. |
| Link % | Polls answered | Above 90%. Retries and backfill cover the rest. |
| Data % | Samples that reached the hub, including backfill | About 100% |
| Backlog | Samples still waiting on a node | Near 0, shrinking after an outage |
| Chip °C | ESP32-S3 temperature | 40–60 °C normal, highlighted at 75 °C or more |
| `R` count | Times a node reset its radio | 0. A count that keeps rising means a weak link or a hardware issue. |

**Troubleshooting:**

| Symptom | Things to check |
|---|---|
| One node is always missing | Its antenna, placement or height |
| Many nodes miss in the same cycle | Interference or the hub. Check hub radio resets on the page. |
| `Radio init failed` | Module seating, pin defines |
| `brownout` on boot | Power supply or cable too weak for 22 dBm TX |

---

## To do / next steps

- [ ] **First compile and hardware test** (1 hub + 1 node, then 3–5 nodes)
- [ ] Replace the placeholder `readSensors()` in `node.ino` with real sensor reads, and update `SensorData` in `protocol.h` if the fields change
- [ ] Range test outdoors and indoors; tune `SF` / antenna placement
- [ ] Optional: persist hub history to flash or microSD so it survives hub power loss
- [ ] Optional: battery operation and sleep on nodes
- [ ] Confirm US regulatory requirements before any product use (915 MHz, 500 kHz, 22 dBm)

**Note:** the radio messages have changed several times during development. Boards running older builds will not talk to this version, so reflash everything.
