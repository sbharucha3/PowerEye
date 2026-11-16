#!/usr/bin/env bash
# Flash the PowerEye lora_star firmware to a node board and a hub board in one go.
#
#   scripts/flash.sh              pick the node (the other board becomes the hub), build + upload both in parallel
#   scripts/flash.sh --id 2       give the node a different ID (default 1)
#   scripts/flash.sh --no-roles   skip setting the role/ID over serial afterwards
#   scripts/flash.sh --dry-run    only list the boards and show what would happen
#   scripts/flash.sh -e <env>     use another PlatformIO env (default lora_star)
#
# Each board gets its own build directory, so the two builds and uploads run side by side.
# Close any open serial monitor first: it holds the port and the upload will fail.
set -uo pipefail
cd "$(dirname "$0")/.."

ENV_NAME=lora_star; NODE_ID=1; DRY=0; ROLES=1
while [ $# -gt 0 ]; do
  case "$1" in
    -e) ENV_NAME="$2"; shift 2 ;;
    --id) NODE_ID="$2"; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    --no-roles) ROLES=0; shift ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown option: $1"; exit 2 ;;
  esac
done

command -v pio >/dev/null || { echo "pio not found. Install PlatformIO Core first."; exit 1; }
PY="$HOME/.platformio/penv/bin/python"; [ -x "$PY" ] || PY=python3

# ---- find boards ----
PORTS=()
while IFS= read -r p; do [ -n "$p" ] && PORTS+=("$p"); done < <(ls /dev/cu.usbmodem* 2>/dev/null)
N=${#PORTS[@]}
if [ "$N" -eq 0 ]; then
  echo "No /dev/cu.usbmodem* devices found. Plug in the boards (hold BOOT while plugging in if needed)."
  exit 1
fi

serial_of() {   # short serial number of a port, to tell boards apart
  pio device list --json-output 2>/dev/null | "$PY" -c '
import json,sys,re
port=sys.argv[1]
for d in json.load(sys.stdin):
    if d.get("port")==port:
        m=re.search(r"SER=([0-9A-Fa-f:]+)",d.get("hwid",""))
        print(m.group(1) if m else "?"); break
' "$1" 2>/dev/null
}

echo
echo "Boards found:"
i=1
for p in "${PORTS[@]}"; do
  printf "  %d) %s   serial %s\n" "$i" "$p" "$(serial_of "$p")"
  i=$((i+1))
done
echo

NODE_PORT=""; HUB_PORT=""
ask_num() {     # prompt, allow_empty -> prints chosen index (1-based) or empty
  local ans
  while true; do
    read -r -p "$1" ans
    [ -z "$ans" ] && [ "$2" = "1" ] && return 0
    if [[ "$ans" =~ ^[0-9]+$ ]] && [ "$ans" -ge 1 ] && [ "$ans" -le "$N" ]; then echo "$ans"; return 0; fi
    echo "  enter a number from 1 to $N" >&2
  done
}

if [ "$N" -eq 1 ]; then
  read -r -p "Only one board found. Is it the (n)ode or the (h)ub? [n/h] " ans
  case "$ans" in n|N) NODE_PORT="${PORTS[0]}" ;; h|H) HUB_PORT="${PORTS[0]}" ;; *) echo "cancelled"; exit 1 ;; esac
else
  tip="Which board is the NODE (the one wired to the ADC/OLED)? [1-$N] "
  a=$(ask_num "$tip" 0); NODE_PORT="${PORTS[$((a-1))]}"
  if [ "$N" -eq 2 ]; then
    for p in "${PORTS[@]}"; do [ "$p" != "$NODE_PORT" ] && HUB_PORT="$p"; done
  else
    b=$(ask_num "Which board is the HUB? [1-$N, Enter to skip] " 1)
    [ -n "$b" ] && HUB_PORT="${PORTS[$((b-1))]}"
    if [ "$HUB_PORT" = "$NODE_PORT" ]; then echo "node and hub must be different boards"; exit 1; fi
  fi
fi

echo
echo "Plan (env: $ENV_NAME):"
[ -n "$NODE_PORT" ] && echo "  NODE -> $NODE_PORT   (role node, id $NODE_ID)"
[ -n "$HUB_PORT" ]  && echo "  HUB  -> $HUB_PORT   (role hub)"
if [ "$DRY" -eq 1 ]; then echo "(dry run: nothing built or flashed)"; exit 0; fi
read -r -p "Go? [Y/n] " go; case "$go" in n|N) echo "cancelled"; exit 1 ;; esac

# ---- libraries once, so the parallel builds don't race on installs ----
echo; echo "Installing libraries..."
pio pkg install -e "$ENV_NAME" >/dev/null 2>&1 || true

# ---- build + upload in parallel, one build dir per board ----
LOGDIR=$(mktemp -d /tmp/powereye_flash.XXXXXX)
PIDS=(); NAMES=()
launch() {   # name port
  local name="$1" port="$2"
  ( PLATFORMIO_BUILD_DIR="$LOGDIR/build_$name" pio run -e "$ENV_NAME" -t upload --upload-port "$port" \
      >"$LOGDIR/$name.log" 2>&1 ) &
  PIDS+=($!); NAMES+=("$name")
  echo "  started $name build + upload ($port)"
}
echo; echo "Building and uploading in parallel (a fresh build takes about a minute)..."
[ -n "$NODE_PORT" ] && launch node "$NODE_PORT"
[ -n "$HUB_PORT" ]  && launch hub  "$HUB_PORT"

FAIL=0
for idx in "${!PIDS[@]}"; do
  if wait "${PIDS[$idx]}"; then echo "  ${NAMES[$idx]}: uploaded OK"
  else echo "  ${NAMES[$idx]}: FAILED (see $LOGDIR/${NAMES[$idx]}.log)"; tail -n 12 "$LOGDIR/${NAMES[$idx]}.log"; FAIL=1; fi
done
[ "$FAIL" -eq 1 ] && { echo; echo "Fix the failure above (hold BOOT while plugging in, close serial monitors) and rerun."; exit 1; }

# ---- set role / ID over serial (saved in flash) ----
send_cmds() {   # port cmd1 cmd2 ...
  local port="$1"; shift
  "$PY" - "$port" "$@" <<'EOF'
import sys, time, serial
port, cmds = sys.argv[1], sys.argv[2:]
def open_port():
    for _ in range(40):
        try: return serial.Serial(port, 115200, timeout=1)
        except Exception: time.sleep(0.5)
    raise SystemExit(f"could not open {port}")
time.sleep(3)                       # let the board boot after the upload
for c in cmds:
    s = open_port(); time.sleep(0.5)
    s.write((c + "\n").encode()); s.flush(); time.sleep(0.5); s.close()
    time.sleep(4)                   # role/id commands reboot the board
print("  sent:", ", ".join(cmds))
EOF
}
if [ "$ROLES" -eq 1 ]; then
  echo; echo "Setting roles over serial..."
  [ -n "$NODE_PORT" ] && { echo " node:"; send_cmds "$NODE_PORT" "role node" "id $NODE_ID"; }
  [ -n "$HUB_PORT" ]  && { echo " hub:";  send_cmds "$HUB_PORT" "role hub"; }
fi

echo; echo "Done. Logs: $LOGDIR"
echo "Check the link:  pio device monitor -b 115200 --port <hub port>   then type: table"
