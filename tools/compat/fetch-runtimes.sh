#!/usr/bin/env bash
#
# Fetch the macOS language runtimes and stage them for an OpenOSX image.
#
# These are the binaries a Mac would actually run - Homebrew's own macOS builds,
# pulled from ghcr.io - not source ports rebuilt with our toolchain. A source
# port proves our compiler works; an unmodified macOS binary proves OpenOSX
# works.
#
#   tools/compat/fetch-runtimes.sh                 # the default set
#   tools/compat/fetch-runtimes.sh --all           # ...plus openjdk (333 MB)
#   tools/compat/fetch-runtimes.sh -o /srv/corpus  # somewhere else
#
# Then build an image carrying them:
#
#   OPENOSX_COMPAT_CORPUS=/root/mac-runtimes nix build .#image
#
# They land at /opt/compat-test in the guest.
#
# Sizes, extracted (measured):
#
#   lua            <1 MB      tcl-tk      42 MB
#   python@3.11    66 MB      node        68 MB
#   perl           75 MB      ruby       108 MB
#   openjdk       333 MB      -> 690 MB for everything
#
# The image's root partition is 4096 MB by default (image.nix rootMB), and the
# base system already occupies most of it. The default set here is 292 MB, which
# fits; --all is 690 MB and needs rootMB raised to match. That is the only
# reason openjdk is opt-in - it works fine, it is just a third of a gigabyte.
set -euo pipefail

OUT=/root/mac-runtimes
TARGET=big_sur
ALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --all)          ALL=1; shift ;;
        --target)       TARGET="$2"; shift 2 ;;
        -o|--out)       OUT="$2"; shift 2 ;;
        -h|--help)      sed -n '2,30p' "$0"; exit 0 ;;
        *)              echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# Chosen for coverage of different runtime shapes rather than popularity:
# a framework build (python), a bare dylib+launcher (ruby, perl), a static-ish
# single binary (lua), and a Tk GUI toolkit (tcl-tk) that will exercise X11.
SET="python@3.11 ruby perl lua tcl-tk node"
[ "$ALL" = "1" ] && SET="$SET openjdk"

here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"

echo "fetching for macOS/$TARGET into $OUT"
# shellcheck disable=SC2086
python3 "$here/fetchbottles.py" $SET --target "$TARGET" -o "$OUT"

echo
echo "staged $(du -sm "$OUT" | cut -f1) MB in $OUT"
echo
echo "build an image carrying them with:"
echo "  OPENOSX_COMPAT_CORPUS=$OUT nix build .#image"
if [ "$ALL" = "1" ]; then
    echo
    echo "NOTE: --all is ~690 MB. Raise rootMB in image.nix (default 4096) or the"
    echo "      root filesystem will run out of space during staging."
fi
