#!/usr/bin/env python3
"""
relocate-bottles - rewrite Homebrew's relocation placeholders in a bottle.

Homebrew bottles are relocatable: their Mach-O load commands carry the literal
tokens @@HOMEBREW_CELLAR@@ and @@HOMEBREW_PREFIX@@, and `brew` rewrites them to
real paths at install time. We extract bottles directly from ghcr.io and never
run brew, so those tokens reach dyld verbatim:

    dyld: Library not loaded: @@HOMEBREW_CELLAR@@/ruby/3.2.2_1/lib/libruby.3.2.dylib

That is not an OpenOSX compatibility gap. It is an install step nobody ran, and
it made ruby, perl, lua and node look broken when they may be fine.

The rewrite is done in place, which is safe here for a specific reason: the
placeholders are 19 characters and the replacement prefix is shorter, so each
path still fits its existing load-command field. The path is written
left-aligned and NUL-padded back to the original field length, leaving every
load command's size - and therefore the whole Mach-O layout - untouched. Growing
a path would require rebuilding the load commands, which this deliberately does
not attempt; it refuses instead.

  relocate-bottles.py /opt/compat-test                  # rewrite in place
  relocate-bottles.py corpus/ --prefix /opt/compat-test # if staged elsewhere
"""
import argparse
import os
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE

LC_ID_DYLIB = 0x0D
LC_LOAD_DYLIB = 0x0C
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_REEXPORT_DYLIB = 0x8000001F
LC_RPATH = 0x8000001C

DYLIB_CMDS = (LC_ID_DYLIB, LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB)
CELLAR = b"@@HOMEBREW_CELLAR@@"
PREFIX = b"@@HOMEBREW_PREFIX@@"


def slices(data):
    """(offset, ) for each 64-bit Mach-O slice. Mirrors machoscan.slices."""
    if len(data) < 8:
        return
    magic, = struct.unpack_from(">I", data, 0)
    if magic == FAT_MAGIC:
        nfat, = struct.unpack_from(">I", data, 4)
        for i in range(nfat):
            _cpu, _sub, off, _size, _align = struct.unpack_from(">5I", data, 8 + i * 20)
            m, = struct.unpack_from("<I", data, off)
            if m == MH_MAGIC_64:
                yield off
        return
    m, = struct.unpack_from("<I", data, 0)
    if m == MH_MAGIC_64:
        yield 0


def rewrite(data, base, newprefix, report):
    """Rewrite placeholder paths in one slice's load commands. Returns count."""
    ncmds, = struct.unpack_from("<I", data, base + 16)
    off = base + 32
    changed = 0
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmdsize == 0:
            break
        if cmd in DYLIB_CMDS or cmd == LC_RPATH:
            # The lc_str is at byte 8 for BOTH shapes:
            #   struct dylib_command { cmd; cmdsize; struct dylib dylib; }
            #   struct dylib { union lc_str name;  <-- offset 8
            #                  timestamp; current_version; compatibility_version; }
            #   struct rpath_command { cmd; cmdsize; union lc_str path; }
            # Reading it at 16 picks up current_version instead, which is why an
            # earlier version of this rewrote 4 paths out of 131: the other 127
            # offsets were nonsense that happened not to contain a placeholder.
            nameoff, = struct.unpack_from("<I", data, off + 8)
            start = off + nameoff
            end = off + cmdsize
            field = data[start:end]
            path = field.split(b"\0", 1)[0]
            if CELLAR not in path and PREFIX not in path:
                off += cmdsize
                continue
            new = path.replace(CELLAR, newprefix).replace(PREFIX, newprefix)
            if len(new) > len(field):
                report.append("REFUSED (would not fit): %s" % path.decode(errors="replace"))
                off += cmdsize
                continue
            data[start:end] = new + b"\0" * (len(field) - len(new))
            changed += 1
        off += cmdsize
    return changed


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("--prefix", default="/opt/compat-test",
                    help="path the corpus will live at in the guest")
    args = ap.parse_args()

    newprefix = args.prefix.encode()
    if len(newprefix) > len(CELLAR):
        sys.exit("--prefix must be at most %d characters; paths are rewritten in "
                 "place and cannot grow" % len(CELLAR))

    magics = (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe")
    files = touched = total = 0
    report = []
    for dirpath, _dirs, names in os.walk(args.root):
        for n in names:
            p = os.path.join(dirpath, n)
            if os.path.islink(p):
                continue
            try:
                with open(p, "rb") as fh:
                    if fh.read(4) not in magics:
                        continue
                data = bytearray(open(p, "rb").read())
            except OSError:
                continue
            files += 1
            c = 0
            for base in slices(data):
                try:
                    c += rewrite(data, base, newprefix, report)
                except (struct.error, IndexError):
                    report.append("PARSE FAILED: %s" % p)
            if c:
                mode = os.stat(p).st_mode
                try:
                    os.chmod(p, mode | 0o200)
                    with open(p, "wb") as fh:
                        fh.write(data)
                    os.chmod(p, mode)
                except OSError as e:
                    report.append("WRITE FAILED %s: %s" % (p, e))
                    continue
                touched += 1
                total += c
    # @@HOMEBREW_PREFIX@@/opt/<formula>/... rewrites to <prefix>/opt/<formula>/...
    # On a real install that is a symlink into the Cellar. Recreate it, or every
    # PREFIX-form path resolves to nothing even though the library is right there.
    links = 0
    optdir = os.path.join(args.root, "opt")
    for name in sorted(os.listdir(args.root)):
        d = os.path.join(args.root, name)
        if name == "opt" or not os.path.isdir(d) or os.path.islink(d):
            continue
        versions = [v for v in sorted(os.listdir(d))
                    if os.path.isdir(os.path.join(d, v))]
        if not versions:
            continue
        os.makedirs(optdir, exist_ok=True)
        link = os.path.join(optdir, name)
        target = os.path.join("..", name, versions[-1])
        try:
            if os.path.islink(link) or os.path.exists(link):
                os.remove(link)
            os.symlink(target, link)
            links += 1
        except OSError as e:
            report.append("SYMLINK FAILED %s: %s" % (link, e))

    print("scanned %d Mach-O files; rewrote %d paths in %d files" % (files, total, touched))
    print("created %d opt/<formula> symlinks into the versioned directories" % links)
    for r in report[:20]:
        print("  " + r)
    return 0


if __name__ == "__main__":
    sys.exit(main())
