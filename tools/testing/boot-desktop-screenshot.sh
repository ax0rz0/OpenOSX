#!/usr/bin/env bash
#
# Boot a built desktop image headless, wait for the XFCE session, and
# screendump the framebuffer via QMP. Produces PNGs of the desktop as it paints.
#
# Two things this script exists to record, both learned the hard way:
#
#   1. Screenshots come from QMP, and the reliable way to get a QMP socket
#      alongside the project's VM runner is to let the runner pass extra qemu
#      args through its trailing "$@": `openosx-vm -display none -qmp ...`.
#      Adding -qmp to a hand-rolled qemu command stalled the guest before the
#      loader; going through the runner's exact, known-good arg set does not.
#
#   2. Serial capture: the runner uses `-serial mon:stdio`, so redirect the
#      runner's stdout to a file. `-serial file:` writes nothing here.
#
# The desktop is software-rendered (WLR_RENDERER=pixman / Xorg + pixman), so on
# TCG (the only option on an AMD host - see docs/COMPAT_STATUS.md, KVM hands XNU
# an AuthenticAMD CPU and it dies at handoff) it takes several minutes to paint
# after the session starts. This screendumps repeatedly to catch it.
#
# Usage: boot-desktop-screenshot.sh <image.img> [out-dir]
set -u

IMG="${1:?usage: boot-desktop-screenshot.sh <image.img> [out-dir]}"
OUT="${2:-./desktop-shots}"
RUN="${OPENOSX_RUNNER:-$(command -v openosx-vm || echo ./result/bin/openosx-vm)}"

mkdir -p "$OUT"
SER="$OUT/serial.log"; QMP="$OUT/qmp.sock"; : > "$SER"

echo "image:  $IMG"
echo "runner: $RUN"
OPENOSX_IMAGE="$IMG" OPENOSX_VM_STATE_DIR="$OUT/state" \
  "$RUN" -display none -qmp "unix:$QMP,server,nowait" > "$SER" 2>&1 &
QEMU=$!

shot() {
  [ -S "$QMP" ] || return
  python3 - "$QMP" "$1" <<'PY'
import socket, json, sys, time
qmp, out = sys.argv[1], sys.argv[2]
try:
    s = socket.socket(socket.AF_UNIX); s.connect(qmp); s.settimeout(6)
    def rd():
        b = b""
        try:
            while b"\n" not in b: b += s.recv(4096)
        except Exception: pass
    rd(); s.sendall(b'{"execute":"qmp_capabilities"}\n'); rd()
    s.sendall(json.dumps({"execute": "screendump",
                          "arguments": {"filename": out}}).encode() + b"\n")
    time.sleep(4)
except Exception as e:
    print("  QMP error:", e)
PY
  echo "  $(basename "$1"): $(stat -c%s "$1" 2>/dev/null || echo 0) bytes"
}

echo "waiting for the desktop session to start (up to 6 min)..."
for i in $(seq 1 72); do
  grep -aqE "openosx-session: starting window manager|startx: launching" "$SER" 2>/dev/null \
    && { echo "  session starting at $((i*5))s"; break; }
  kill -0 "$QEMU" 2>/dev/null || { echo "  qemu exited at $((i*5))s"; break; }
  sleep 5
done

echo "screendumping every 2 min for 16 min while it paints..."
for m in $(seq 1 8); do
  sleep 120
  kill -0 "$QEMU" 2>/dev/null || break
  shot "$OUT/paint-$(printf '%02d' $((m*2)))min.ppm"
done

# PPM -> PNG if netpbm is available.
for ppm in "$OUT"/*.ppm; do
  [ -f "$ppm" ] || continue
  pnmtopng "$ppm" > "${ppm%.ppm}.png" 2>/dev/null || true
done

echo; echo "=== desktop session markers ==="
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' \
  | grep -aE "openosx-session:|xfwm4|xfce4-panel|xfdesktop|window manager|panic" \
  | awk '!s[$0]++' | tail -20

kill "$QEMU" 2>/dev/null
echo "serial log: $SER ($(wc -c < "$SER") bytes)"
