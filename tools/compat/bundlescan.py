#!/usr/bin/env python3
"""
bundlescan - what a whole .app bundle needs *from the system*.

Scanning only Contents/MacOS/<exe> understates real apps badly: VLC's
executable is a 62-symbol launcher that dlopens libvlc, and the actual
dependencies live in the bundle's own dylibs and frameworks.

This walks every Mach-O in the bundle, unions their undefined symbols, then
subtracts everything the bundle itself exports - because an app that ships a
library does not need the system to provide it. What remains is the true
system requirement.

  bundlescan.py <Some.app> -i inventory.json [-o report.json]
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from machoscan import slices, parse                        # noqa: E402
from coverage import exported                              # noqa: E402

MAGICS = (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca')


def is_macho(path):
    try:
        with open(path, 'rb') as f:
            return f.read(4) in MAGICS
    except OSError:
        return False


def walk(bundle):
    for root, _dirs, files in os.walk(bundle):
        for fn in files:
            p = os.path.join(root, fn)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            if is_macho(p):
                yield p


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    bundle = sys.argv[1]
    invp = sys.argv[sys.argv.index('-i') + 1] if '-i' in sys.argv else 'inventory.json'
    inv = json.load(open(invp))
    have = set(inv['symbols'])

    machos = sorted(walk(bundle))
    print('bundle: %s' % bundle)
    print('mach-o files: %d' % len(machos))

    needed = {}        # symbol -> library it is expected from
    provided = set()   # everything the bundle ships itself
    selectors, classrefs, superrefs = set(), 0, 0
    minos = None

    for p in machos:
        data = open(p, 'rb').read()
        # what this file exports (it ships inside the bundle)
        s, _c = exported(p)
        provided |= s
        # what it needs
        for _cputype, base in slices(data):
            try:
                r = parse(data, base)
            except Exception:
                continue
            if r['arch'] != 'x86_64':
                continue
            if minos is None and r['minos']:
                minos = r['minos']
            for u in r['undefined']:
                needed.setdefault(u['symbol'], os.path.basename(u['library']))
            selectors |= set(r['objc_selectors'])
            classrefs += r['objc_classrefs']
            superrefs += r['objc_superrefs']

    # Symbols the bundle satisfies internally are not a system requirement.
    system_needed = {k: v for k, v in needed.items() if k not in provided}
    missing = {k: v for k, v in system_needed.items() if k not in have}
    total = len(system_needed) or 1
    cov = 100.0 * (len(system_needed) - len(missing)) / total

    bylib = {}
    for sym, lib in missing.items():
        bylib.setdefault(lib, []).append(sym)

    print()
    print('  minos                    : %s' % minos)
    print('  symbols needed in total  : %d' % len(needed))
    print('  satisfied inside bundle  : %d' % (len(needed) - len(system_needed)))
    print('  required from the system : %d' % len(system_needed))
    print('  OpenOSX provides         : %d  (%.1f%%)' % (len(system_needed) - len(missing), cov))
    print('  still missing            : %d' % len(missing))
    print('  objc selectors sent      : %d' % len(selectors))
    print('  objc classrefs           : %d' % classrefs)
    print('  objc superrefs           : %d   <- subclassed, the expensive ones' % superrefs)
    print()
    print('  missing by library:')
    for lib, syms in sorted(bylib.items(), key=lambda kv: -len(kv[1]))[:18]:
        print('    %-34s %5d' % (lib, len(syms)))

    if '-o' in sys.argv:
        out = sys.argv[sys.argv.index('-o') + 1]
        json.dump({
            'bundle': bundle, 'minos': minos, 'machos': len(machos),
            'needed_total': len(needed),
            'system_needed': len(system_needed),
            'provided': len(system_needed) - len(missing),
            'coverage': round(cov, 1),
            'missing_by_library': {k: sorted(v) for k, v in bylib.items()},
            'objc_selectors': len(selectors),
            'objc_superrefs': superrefs,
        }, open(out, 'w'), indent=1)
        print('\nwrote %s' % out)


if __name__ == '__main__':
    main()
