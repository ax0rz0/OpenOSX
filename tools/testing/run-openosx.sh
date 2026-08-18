#!/usr/bin/env bash
#
# Boot OpenOSX in a window. This is the "just run it" entry point.
#
#   tools/testing/run-openosx.sh                 # newest built image, GUI window
#   tools/testing/run-openosx.sh --console       # no window, serial on this terminal
#   tools/testing/run-openosx.sh --image PATH    # a specific image
#   tools/testing/run-openosx.sh --build         # build .#image first, then boot
#
# Notes that matter on this host:
#
#  - TCG only, no KVM. The runner passes -cpu IvyBridge,vendor=GenuineIntel
#    because vanilla XNU x86_64 assumes an Intel CPUID vendor and dies at handoff
#    on an AuthenticAMD one. `.#kvm-runner` uses -cpu host and therefore does NOT
#    work on this machine; that is a CPU-masquerading problem, not a QEMU flag to
#    tweak.
#  - Software rendering, so first paint takes several minutes after the session
#    starts. A blank grey screen for a while is normal, not a hang.
#  - The store image is read-only, so QEMU is given snapshot=on and nothing you
#    do inside the VM persists. Copy the image somewhere writable if you want it
#    to stick.
set -uo pipefail

IMAGE=""
CONSOLE=0
BUILD=0
EXTRA=()

while [ $# -gt 0 ]; do
    case "$1" in
        --image)   IMAGE="$2"; shift 2 ;;
        --console) CONSOLE=1; shift ;;
        --build)   BUILD=1; shift ;;
        -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
        *)         EXTRA+=("$1"); shift ;;
    esac
done

if [ -f /root/.nix-profile/etc/profile.d/nix.sh ]; then
    . /root/.nix-profile/etc/profile.d/nix.sh
fi

REPO=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO"

if [ "$BUILD" = "1" ]; then
    echo "building .#image (hours if libSystem changed, minutes otherwise)"
    nix build .#image -o ./result-image --print-build-logs || exit 1
fi

if [ -z "$IMAGE" ]; then
    if [ -e ./result-image/openosx.img ]; then
        IMAGE=$(readlink -f ./result-image)/openosx.img
    else
        # Newest built image in the store. --dry-run would tell us the current
        # target, but that is slow and usually not what someone wants when they
        # just typed "run it".
        IMAGE=$(ls -t /nix/store/*-openosx-image-*/openosx.img 2>/dev/null | head -1)
    fi
fi
[ -n "$IMAGE" ] && [ -f "$IMAGE" ] || {
    echo "no image found. Build one with:  $0 --build" >&2
    exit 1
}

RUNNER=./result-vm/bin/openosx-vm
if [ ! -x "$RUNNER" ]; then
    echo "building the VM runner"
    nix build .#vm-runner -o ./result-vm || exit 1
fi

echo "image:  $IMAGE ($(( $(stat -c%s "$IMAGE") / 1024 / 1024 )) MB)"
echo "runner: $(readlink -f "$RUNNER")"
echo

export OPENOSX_IMAGE="$IMAGE"
export OPENOSX_VM_STATE_DIR="${OPENOSX_VM_STATE_DIR:-$REPO/.openosx-vm}"

if [ "$CONSOLE" = "1" ]; then
    echo "console mode: serial on this terminal, no window. Ctrl-A X to quit."
    exec "$RUNNER" -display none "${EXTRA[@]+"${EXTRA[@]}"}"
fi

echo "opening a window. Software rendering - the desktop takes a few minutes to paint."
echo "Serial output appears here too; Ctrl-A X quits QEMU."
exec "$RUNNER" "${EXTRA[@]+"${EXTRA[@]}"}"
