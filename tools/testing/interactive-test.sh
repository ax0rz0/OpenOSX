#!/usr/bin/env bash
# Boot the image and interact with the console shell over serial stdio.
set -u
W=/root/boottest
LOG=$W/serial3.log
FIFO=$W/in
IMG="${1:-/root/openosx/result/puredarwin-minimal.img}"
mkdir -p "$W"
rm -f "$FIFO"; mkfifo "$FIFO"
: > "$LOG"

PUREDARWIN_IMAGE="$IMG" PUREDARWIN_VM_STATE_DIR=$W/state \
  timeout 480 /root/vmrun/bin/puredarwin-vm -display none \
  < "$FIFO" > "$LOG" 2>&1 &
VMPID=$!
exec 3> "$FIFO"   # hold writer open so qemu's stdin stays alive

shell_up=0
for i in $(seq 1 90); do
  if grep -aq 'exec /bin/zsh' "$LOG"; then shell_up=1; break; fi
  kill -0 $VMPID 2>/dev/null || break
  sleep 5
done

if [ "$shell_up" = 1 ]; then
  sleep 10
  printf 'echo HELLO_OPENOSX_$((6*7))\r' >&3;  sleep 8
  printf 'uname -a\r' >&3;                     sleep 6
  printf 'sysctl kern.ostype kern.osrelease hw.ncpu\r' >&3; sleep 6
  printf 'ls /\r' >&3;                         sleep 6
fi

kill $VMPID 2>/dev/null
exec 3>&-
sleep 2

echo "=== RESULTS ==="
grep -a 'HELLO_OPENOSX_42' "$LOG" | head -3
echo "--- uname ---"
grep -a 'Darwin.*RELEASE_X86_64\|Darwin.*xnu' "$LOG" | tail -2
echo "--- sysctl ---"
grep -a 'kern.ostype\|kern.osrelease\|hw.ncpu' "$LOG" | head -6
echo "--- ls / ---"
tail -15 "$LOG" | tr -d '\r' | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' | grep -v '^$' | tail -8
