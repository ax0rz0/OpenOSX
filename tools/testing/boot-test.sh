#!/usr/bin/env bash
# Headless boot test of a built OpenOSX/OpenOSX image inside WSL.
# Usage: boot-test.sh <image> [timeout_sec] [pass_regex]
set -u
IMG="${1:?usage: boot-test.sh <image> [timeout] [pass_regex]}"
TIMEOUT="${2:-420}"
PASS="${3:-Darwin Kernel Version}"
WORK=/root/boottest
mkdir -p "$WORK"
SERIAL="$WORK/serial.log"
: > "$SERIAL"

# OVMF from the nix store (qemu + OVMF via nix run used by upstream; here we
# use system qemu if present, else nix shell)
if ! command -v qemu-system-x86_64 >/dev/null; then
  echo "FATAL: qemu-system-x86_64 not in PATH" >&2; exit 2
fi
OVMF_CODE=$(ls /nix/store/*OVMF*/FV/OVMF_CODE.fd 2>/dev/null | head -1)
if [ -z "$OVMF_CODE" ]; then
  OVMF_CODE=$(find /usr/share/OVMF -name 'OVMF_CODE*.fd' 2>/dev/null | head -1)
fi
[ -z "$OVMF_CODE" ] && { echo "FATAL: no OVMF firmware found" >&2; exit 2; }
OVMF_VARS_SRC=
cp -f "$OVMF_VARS_SRC" "$WORK/OVMF_VARS.fd" 2>/dev/null || true

qemu-system-x86_64 \
  -M q35 -accel tcg -m 4096 -smp 4 \
  -cpu IvyBridge,vendor=GenuineIntel \
  -fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=2048 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
  ${OVMF_VARS_SRC:+-drive if=pflash,format=raw,unit=1,file="$WORK/OVMF_VARS.fd"} \
  -drive id=root,format=raw,file="$IMG",snapshot=on \
  -device qemu-xhci,id=xhci \
  -device usb-kbd,bus=xhci.0 \
  -device usb-mouse,bus=xhci.0 \
  -vga std -display none \
  -serial "file:$SERIAL" \
  -no-reboot &
QPID=$!

verdict="TIMEOUT"
end=$((SECONDS + TIMEOUT))
while [ $SECONDS -lt $end ]; do
  sleep 5
  if ! kill -0 $QPID 2>/dev/null; then verdict="QEMU_EXITED"; break; fi
  if grep -qE "$PASS" "$SERIAL" 2>/dev/null; then verdict="PASS"; break; fi
  if grep -qE "Kernel panic|panic\(cpu" "$SERIAL" 2>/dev/null; then
    sleep 10  # let the panic log finish writing
    verdict="PANIC"; break
  fi
done
kill $QPID 2>/dev/null

echo "VERDICT: $verdict"
echo "=== serial tail ==="
tail -30 "$SERIAL" 2>/dev/null
[ "$verdict" = "PASS" ]
