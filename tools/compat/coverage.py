#!/usr/bin/env python3
"""
coverage - how much of what a macOS app needs does OpenOSX actually provide?

Builds an inventory of every symbol and Objective-C class exported by the
dylibs in an OpenOSX image, then diffs an app's requirements against it.

Progress on macOS compatibility is otherwise unmeasurable: this turns "run Mac
apps" into a ranked list of named, finite work items.

  coverage.py inventory <dir-of-dylibs>... -o inventory.json
  coverage.py report <app-binary-or-.app> -i inventory.json [-o report.json]

An inventory root may also be an ld64 `-exported_symbols_list` allow-list, such
as src/Libraries/libSystem/stub/libSystem.exports. That file *is* the shipped
libSystem's export surface - it is applied together with -dead_strip, so a
symbol absent from it is discarded from the dylib even when its code was
compiled and linked. Reading it directly makes coverage measurable straight
from a source checkout, with no build, no image and no VM, which is what lets
CI track this per-commit.

Note the deliberate three axes. Symbol coverage alone overstates readiness
badly, because Objective-C message sends are not symbols: a binary can link
perfectly and still die on the first unimplemented selector.
"""
import json
import os
import struct
import sys
import glob

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from machoscan import slices, parse, find_binary          # noqa: E402

LC_SYMTAB = 0x02


def exported(path):
    """Exported (defined, external) symbols of a Mach-O dylib."""
    try:
        data = open(path, 'rb').read()
    except OSError:
        return set(), set()
    syms, classes = set(), set()
    for _cputype, base in slices(data):
        try:
            ncmds, = struct.unpack_from('<I', data, base + 16)
        except struct.error:
            continue
        off, symoff, nsyms, stroff = base + 32, 0, 0, 0
        for _ in range(ncmds):
            try:
                cmd, cmdsize = struct.unpack_from('<II', data, off)
            except struct.error:
                break
            if cmd == LC_SYMTAB:
                symoff, nsyms, stroff, _ = struct.unpack_from('<IIII', data, off + 8)
            off += cmdsize
        for i in range(nsyms):
            try:
                n_strx, n_type, _sect, _desc, n_value = struct.unpack_from(
                    '<IBBHQ', data, base + symoff + i * 16)
            except struct.error:
                break
            if not (n_type & 0x01):            # N_EXT
                continue
            if (n_type & 0x0e) == 0x0:         # N_UNDF: imported, not exported
                continue
            try:
                end = data.index(b'\0', base + stroff + n_strx)
            except ValueError:
                continue
            name = data[base + stroff + n_strx:end].decode(errors='replace')
            if not name:
                continue
            syms.add(name)
            if name.startswith('_OBJC_CLASS_$_'):
                classes.add(name[len('_OBJC_CLASS_$_'):])
    return syms, classes


# libSystem.B.dylib re-exports /usr/lib/system/libdyld.dylib
# (-Wl,-reexport_library, see src/Libraries/libSystem/stub/CMakeLists.txt), so
# libdyld's symbols resolve against libSystem without appearing in its own
# allow-list. Only the ones a normal binary actually imports are listed here;
# this is a floor, not libdyld's full surface.
REEXPORTED_FROM_LIBDYLD = {
    'dyld_stub_binder',          # every classic lazy-bound binary imports this
}


def exported_list(path):
    """Symbols named by an ld64 -exported_symbols_list allow-list."""
    syms, classes = set(), set()
    with open(path, errors='replace') as fh:
        for line in fh:
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            syms.add(line)
            if line.startswith('_OBJC_CLASS_$_'):
                classes.add(line[len('_OBJC_CLASS_$_'):])
    return syms, classes


def build_inventory(roots):
    syms, classes, libs = set(), set(), []
    seen = set()
    for root in roots:
        if os.path.isfile(root):                       # an allow-list, not a tree
            s, c = exported_list(root)
            if root.endswith('.exports'):
                s |= REEXPORTED_FROM_LIBDYLD
            libs.append({'path': root, 'symbols': len(s), 'classes': len(c)})
            syms |= s
            classes |= c
            continue
        for pat in ('**/*.dylib', '**/*.tbd', '**/*.exports', '**/lib*'):
            for p in glob.glob(os.path.join(root, pat), recursive=True):
                if not os.path.isfile(p) or p in seen:
                    continue
                seen.add(p)
                s, c = exported_list(p) if p.endswith('.exports') else exported(p)
                if p.endswith('.exports'):
                    s |= REEXPORTED_FROM_LIBDYLD
                if s:
                    libs.append({'path': p, 'symbols': len(s), 'classes': len(c)})
                    syms |= s
                    classes |= c
    return {'symbols': sorted(syms), 'classes': sorted(classes), 'libraries': libs}


def report(app, inv):
    have = set(inv['symbols'])
    have_classes = set(inv['classes'])
    binary = find_binary(app)
    data = open(binary, 'rb').read()

    out = {'binary': binary, 'slices': []}
    for _cputype, base in slices(data):
        r = parse(data, base)
        if r['arch'] != 'x86_64':
            continue
        missing, present = [], []
        for u in r['undefined']:
            (present if u['symbol'] in have else missing).append(u)
        bylib = {}
        for u in missing:
            bylib.setdefault(os.path.basename(u['library']), []).append(u['symbol'])
        total = len(r['undefined']) or 1
        out['slices'].append({
            'arch': r['arch'], 'minos': r['minos'],
            'symbols_total': len(r['undefined']),
            'symbols_present': len(present),
            'symbols_missing': len(missing),
            'symbol_coverage': round(100.0 * len(present) / total, 1),
            'missing_by_library': {k: sorted(v) for k, v in
                                   sorted(bylib.items(), key=lambda kv: -len(kv[1]))},
            'objc_selectors': len(r['objc_selectors']),
            'objc_classrefs': r['objc_classrefs'],
            'objc_superrefs': r['objc_superrefs'],
            'dylibs': r['dylibs'],
        })
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    mode = sys.argv[1]

    if mode == 'inventory':
        roots = [a for a in sys.argv[2:] if not a.startswith('-')]
        outp = sys.argv[sys.argv.index('-o') + 1] if '-o' in sys.argv else 'inventory.json'
        inv = build_inventory(roots)
        json.dump(inv, open(outp, 'w'))
        print('inventory: %d symbols, %d ObjC classes, from %d libraries'
              % (len(inv['symbols']), len(inv['classes']), len(inv['libraries'])))
        for l in sorted(inv['libraries'], key=lambda x: -x['symbols'])[:12]:
            print('  %-70s %6d syms' % (os.path.basename(l['path'])[:70], l['symbols']))
        print('wrote %s' % outp)

    elif mode == 'report':
        app = sys.argv[2]
        invp = sys.argv[sys.argv.index('-i') + 1] if '-i' in sys.argv else 'inventory.json'
        inv = json.load(open(invp))
        rep = report(app, inv)
        print('app: %s' % rep['binary'])
        for s in rep['slices']:
            print()
            print('  minos %s   symbol coverage %.1f%%  (%d of %d)'
                  % (s['minos'], s['symbol_coverage'], s['symbols_present'], s['symbols_total']))
            print('  objc: %d selectors, %d classrefs, %d superrefs'
                  % (s['objc_selectors'], s['objc_classrefs'], s['objc_superrefs']))
            print('  missing symbols by library:')
            for lib, syms in s['missing_by_library'].items():
                print('    %-32s %4d' % (lib, len(syms)))
            print()
            print('  first missing symbols per library (the actual work list):')
            for lib, syms in list(s['missing_by_library'].items())[:6]:
                print('    %s:' % lib)
                for sym in syms[:8]:
                    print('        %s' % sym)
                if len(syms) > 8:
                    print('        ... and %d more' % (len(syms) - 8))
        if '-o' in sys.argv:
            outp = sys.argv[sys.argv.index('-o') + 1]
            json.dump(rep, open(outp, 'w'), indent=1)
            print('\nwrote %s' % outp)
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
