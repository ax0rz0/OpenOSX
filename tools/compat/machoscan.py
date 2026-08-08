#!/usr/bin/env python3
"""
machoscan - what a Mach-O binary demands of the system it runs on.

Pure-Python Mach-O reader: no otool, no nm, no macOS. Given an executable or a
.app bundle it reports the four things that decide whether OpenOSX can run it:

  platform      LC_BUILD_VERSION / LC_VERSION_MIN_MACOSX. dyld refuses a binary
                whose minimum OS is newer than the running system, so this gate
                precedes everything else.
  dylibs        LC_LOAD_DYLIB and friends. A missing strong dylib is a hard
                launch failure; a weak one is survivable.
  symbols       Undefined externals from LC_SYMTAB, attributed to the library
                each one is expected to come from (two-level namespace ordinal).
  objc          __objc_selrefs / __objc_classrefs / __objc_superrefs. Objective-C
                message sends are NOT symbols, so symbol coverage alone badly
                overstates readiness; superrefs are the expensive ones, because
                subclassing needs a compatible ivar layout, not just methods.

Usage:
  machoscan.py <path-to-binary-or-.app> [--json out.json]
"""
import json
import struct
import sys
import os

MH_MAGIC_64 = 0xfeedfacf
MH_CIGAM_64 = 0xcffaedfe
FAT_MAGIC = 0xcafebabe
FAT_CIGAM = 0xbebafeca

LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x02
LC_LOAD_DYLIB = 0x0c
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_REEXPORT_DYLIB = 0x8000001f
LC_LOAD_UPWARD_DYLIB = 0x80000023
LC_VERSION_MIN_MACOSX = 0x24
LC_BUILD_VERSION = 0x32

CPU_TYPE_X86_64 = 0x01000007
CPU_TYPE_ARM64 = 0x0100000c
CPU_NAMES = {CPU_TYPE_X86_64: 'x86_64', CPU_TYPE_ARM64: 'arm64'}


def find_binary(path):
    """Accept a bundle or a plain executable."""
    if os.path.isdir(path) and path.rstrip('/').endswith('.app'):
        macos = os.path.join(path, 'Contents', 'MacOS')
        if os.path.isdir(macos):
            entries = [os.path.join(macos, e) for e in sorted(os.listdir(macos))]
            files = [e for e in entries if os.path.isfile(e) and os.access(e, os.X_OK)]
            if files:
                return files[0]
        raise SystemExit('no executable found in %s' % macos)
    return path


def slices(data):
    """Yield (cputype, offset) for each 64-bit Mach-O slice."""
    magic, = struct.unpack_from('>I', data, 0)
    if magic in (FAT_MAGIC, FAT_CIGAM):
        nfat, = struct.unpack_from('>I', data, 4)
        for i in range(nfat):
            cputype, _sub, off, _size, _align = struct.unpack_from('>5I', data, 8 + i * 20)
            m, = struct.unpack_from('<I', data, off)
            if m == MH_MAGIC_64:
                yield cputype | 0x01000000 if cputype < 0x01000000 else cputype, off
        return
    m, = struct.unpack_from('<I', data, 0)
    if m == MH_MAGIC_64:
        cputype, = struct.unpack_from('<i', data, 4)
        yield cputype & 0xffffffff, 0


def parse(data, base):
    out = {
        'arch': None, 'platform': None, 'minos': None, 'sdk': None,
        'dylibs': [], 'weak_dylibs': [], 'undefined': [],
        'objc_selectors': [], 'objc_classrefs': 0, 'objc_superrefs': 0,
    }
    cputype, = struct.unpack_from('<I', data, base + 4)
    out['arch'] = CPU_NAMES.get(cputype, hex(cputype))
    ncmds, = struct.unpack_from('<I', data, base + 16)

    off = base + 32
    symoff = nsyms = stroff = 0
    ordinals = []      # dylib load order, for two-level namespace attribution
    sections = {}      # (seg,sect) -> (offset, size)

    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', data, off)

        if cmd == LC_SEGMENT_64:
            nsects, = struct.unpack_from('<I', data, off + 64)
            so = off + 72
            for _ in range(nsects):
                sect = data[so:so + 16].rstrip(b'\0').decode(errors='replace')
                seg = data[so + 16:so + 32].rstrip(b'\0').decode(errors='replace')
                offset, = struct.unpack_from('<I', data, so + 48)
                size, = struct.unpack_from('<Q', data, so + 40)
                sections[(seg, sect)] = (offset, size)
                so += 80

        elif cmd == LC_SYMTAB:
            symoff, nsyms, stroff, _strsize = struct.unpack_from('<IIII', data, off + 8)

        elif cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LOAD_UPWARD_DYLIB):
            noff, = struct.unpack_from('<I', data, off + 8)
            end = data.index(b'\0', off + noff)
            name = data[off + noff:end].decode(errors='replace')
            ordinals.append(name)
            if cmd == LC_LOAD_WEAK_DYLIB:
                out['weak_dylibs'].append(name)
            else:
                out['dylibs'].append(name)

        elif cmd == LC_BUILD_VERSION:
            platform, minos, sdk = struct.unpack_from('<III', data, off + 8)
            out['platform'] = {1: 'macOS', 2: 'iOS', 3: 'tvOS', 6: 'macCatalyst'}.get(platform, platform)
            fmt = lambda v: '%d.%d.%d' % (v >> 16, (v >> 8) & 0xff, v & 0xff)
            out['minos'], out['sdk'] = fmt(minos), fmt(sdk)

        elif cmd == LC_VERSION_MIN_MACOSX:
            ver, sdk = struct.unpack_from('<II', data, off + 8)
            fmt = lambda v: '%d.%d.%d' % (v >> 16, (v >> 8) & 0xff, v & 0xff)
            out['platform'], out['minos'], out['sdk'] = 'macOS', fmt(ver), fmt(sdk)

        off += cmdsize

    # Undefined externals, attributed to the dylib they are expected from.
    if nsyms:
        for i in range(nsyms):
            n_strx, n_type, _n_sect, n_desc, _n_value = struct.unpack_from(
                '<IBBHQ', data, base + symoff + i * 16)
            if (n_type & 0x0e) != 0x0:      # N_UNDF
                continue
            if not (n_type & 0x01):          # N_EXT
                continue
            end = data.index(b'\0', base + stroff + n_strx)
            name = data[base + stroff + n_strx:end].decode(errors='replace')
            lib_ord = (n_desc >> 8) & 0xff
            lib = ordinals[lib_ord - 1] if 1 <= lib_ord <= len(ordinals) else 'flat/unknown'
            out['undefined'].append({'symbol': name, 'library': lib})

    # Objective-C: selectors actually sent, classes referenced and subclassed.
    if ('__TEXT', '__objc_methname') in sections:
        o, sz = sections[('__TEXT', '__objc_methname')]
        blob = data[base + o:base + o + sz]
        out['objc_selectors'] = [s.decode(errors='replace') for s in blob.split(b'\0') if s]
    for seg in ('__DATA', '__DATA_CONST'):
        if (seg, '__objc_classrefs') in sections:
            out['objc_classrefs'] += sections[(seg, '__objc_classrefs')][1] // 8
        if (seg, '__objc_superrefs') in sections:
            out['objc_superrefs'] += sections[(seg, '__objc_superrefs')][1] // 8
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    target = find_binary(sys.argv[1])
    data = open(target, 'rb').read()

    results = []
    for cputype, off in slices(data):
        r = parse(data, off)
        results.append(r)

    print('binary: %s (%s)' % (target, ', '.join(r['arch'] for r in results)))
    for r in results:
        print()
        print('=== %s slice ===' % r['arch'])
        print('  platform      : %s, minos %s (SDK %s)' % (r['platform'], r['minos'], r['sdk']))
        print('  dylibs        : %d strong, %d weak' % (len(r['dylibs']), len(r['weak_dylibs'])))
        for d in r['dylibs']:
            print('      %s' % d)
        if r['weak_dylibs']:
            print('    weak:')
            for d in r['weak_dylibs']:
                print('      %s' % d)
        print('  undefined syms: %d' % len(r['undefined']))
        bylib = {}
        for u in r['undefined']:
            bylib.setdefault(u['library'], []).append(u['symbol'])
        for lib, syms in sorted(bylib.items(), key=lambda kv: -len(kv[1])):
            print('      %-60s %d' % (os.path.basename(lib), len(syms)))
        print('  objc selectors: %d' % len(r['objc_selectors']))
        print('  objc classrefs: %d' % r['objc_classrefs'])
        print('  objc superrefs: %d   <- subclassed; the expensive ones' % r['objc_superrefs'])

    if '--json' in sys.argv:
        out = sys.argv[sys.argv.index('--json') + 1]
        json.dump({'binary': target, 'slices': results}, open(out, 'w'), indent=1)
        print('\nwrote %s' % out)


if __name__ == '__main__':
    main()
