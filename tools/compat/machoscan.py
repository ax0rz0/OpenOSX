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

  loader        How dyld is expected to fix the binary up: chained fixups vs the
                classic bind opcodes, the entry-point style, @rpath use, TLS.
                A binary whose fixup format we do not implement cannot load at
                all, so this gate is independent of - and prior to - symbols.

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

LC_UNIXTHREAD = 0x05
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x80000022
LC_RPATH = 0x8000001c
LC_CODE_SIGNATURE = 0x1d
LC_ENCRYPTION_INFO_64 = 0x2c
LC_MAIN = 0x80000028
LC_DYLD_EXPORTS_TRIE = 0x80000033
LC_DYLD_CHAINED_FIXUPS = 0x80000034

CPU_TYPE_X86_64 = 0x01000007
CPU_TYPE_ARM64 = 0x0100000c
CPU_NAMES = {CPU_TYPE_X86_64: 'x86_64', CPU_TYPE_ARM64: 'arm64'}

FILETYPES = {2: 'executable', 6: 'dylib', 8: 'bundle', 7: 'dylinker', 5: 'core'}

# Header flags that change what the loader must do.
MH_FLAGS = [
    (0x00000004, 'DYLDLINK'), (0x00000080, 'TWOLEVEL'),
    (0x00001000, 'WEAK_DEFINES'), (0x00002000, 'BINDS_TO_WEAK'),
    (0x00200000, 'PIE'), (0x00800000, 'HAS_TLV_DESCRIPTORS'),
    (0x80000000, 'DYLIB_IN_CACHE'),
]

# dyld_chained_starts_in_segment.pointer_format. Each is a distinct walker in
# the loader, so knowing which one a binary uses is the whole implementation
# question, not a detail.
CHAINED_PTR_FORMATS = {
    1: 'ARM64E', 2: '64', 3: '32', 4: '32_CACHE', 5: '32_FIRMWARE',
    6: '64_OFFSET', 7: 'ARM64E_KERNEL', 8: '64_KERNEL_CACHE',
    9: 'ARM64E_USERLAND', 10: 'ARM64E_FIRMWARE', 11: 'X86_64_KERNEL_CACHE',
    12: 'ARM64E_USERLAND24',
}
CHAINED_IMPORT_FORMATS = {1: 'IMPORT', 2: 'IMPORT_ADDEND', 3: 'IMPORT_ADDEND64'}


def _chained_detail(data, base, dataoff, datasize):
    """Read dyld_chained_fixups_header and the per-segment pointer formats.

    Reported because 'uses chained fixups' is not actionable on its own: the
    loader needs the specific pointer format and import format to walk them.
    """
    out = {'imports': None, 'import_format': None, 'pointer_formats': []}
    try:
        (_ver, starts_off, _imp_off, _sym_off,
         imports_count, imports_format, _sym_fmt) = struct.unpack_from(
            '<7I', data, base + dataoff)
    except struct.error:
        return out
    out['imports'] = imports_count
    out['import_format'] = CHAINED_IMPORT_FORMATS.get(imports_format, imports_format)

    try:
        seg_count, = struct.unpack_from('<I', data, base + dataoff + starts_off)
    except struct.error:
        return out
    fmts = []
    for i in range(min(seg_count, 64)):
        try:
            seg_info_off, = struct.unpack_from(
                '<I', data, base + dataoff + starts_off + 4 + i * 4)
        except struct.error:
            break
        if seg_info_off == 0:                  # segment has no fixups
            continue
        try:
            _size, _page_size, ptr_fmt = struct.unpack_from(
                '<IHH', data, base + dataoff + starts_off + seg_info_off)
        except struct.error:
            continue
        name = CHAINED_PTR_FORMATS.get(ptr_fmt, str(ptr_fmt))
        if name not in fmts:
            fmts.append(name)
    out['pointer_formats'] = fmts
    return out


def find_binary(path):
    """Accept a bundle or a plain executable.

    Deliberately does not require the execute bit: a bundle pulled out of a
    DMG with 7z (or any archive tool that drops HFS+ permissions) arrives
    without one, and what makes a file the app binary is its Mach-O magic,
    not its mode.
    """
    if os.path.isdir(path) and path.rstrip('/').endswith('.app'):
        macos = os.path.join(path, 'Contents', 'MacOS')
        if os.path.isdir(macos):
            entries = [os.path.join(macos, e) for e in sorted(os.listdir(macos))]
            magics = (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe')
            for e in entries:
                if not os.path.isfile(e):
                    continue
                try:
                    with open(e, 'rb') as fh:
                        if fh.read(4) in magics:
                            return e
                except OSError:
                    continue
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
        'filetype': None, 'flags': [], 'fixups': None, 'chained': None,
        'entry': None, 'rpaths': [], 'code_signature': False,
        'encrypted': False, 'tls': False,
    }
    cputype, = struct.unpack_from('<I', data, base + 4)
    out['arch'] = CPU_NAMES.get(cputype, hex(cputype))
    filetype, ncmds, _sizeofcmds, flags = struct.unpack_from('<IIII', data, base + 12)
    out['filetype'] = FILETYPES.get(filetype, filetype)
    out['flags'] = [n for bit, n in MH_FLAGS if flags & bit]

    off = base + 32
    symoff = nsyms = stroff = 0
    chained_data = None
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

        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            out['fixups'] = 'chained'
            chained_data = struct.unpack_from('<II', data, off + 8)

        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            # Only classic if nothing later sets 'chained'; resolved after the loop.
            out['fixups'] = out['fixups'] or 'classic'

        elif cmd == LC_MAIN:
            out['entry'] = 'LC_MAIN'
        elif cmd == LC_UNIXTHREAD:
            out['entry'] = out['entry'] or 'LC_UNIXTHREAD'

        elif cmd == LC_RPATH:
            noff, = struct.unpack_from('<I', data, off + 8)
            end = data.index(b'\0', off + noff)
            out['rpaths'].append(data[off + noff:end].decode(errors='replace'))

        elif cmd == LC_CODE_SIGNATURE:
            out['code_signature'] = True
        elif cmd == LC_ENCRYPTION_INFO_64:
            _o, _s, cryptid = struct.unpack_from('<III', data, off + 8)
            out['encrypted'] = cryptid != 0

        off += cmdsize

    if chained_data:
        out['chained'] = _chained_detail(data, base, *chained_data)
    out['tls'] = ('__DATA', '__thread_vars') in sections or \
                 ('__DATA_CONST', '__thread_vars') in sections

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
        print('  filetype      : %s  [%s]' % (r['filetype'], ' '.join(r['flags'])))
        fx = r['fixups'] or 'none'
        if r['chained']:
            fx += '  (ptr %s, %s, %s imports)' % (
                '/'.join(r['chained']['pointer_formats']) or '?',
                r['chained']['import_format'], r['chained']['imports'])
        print('  fixups        : %s' % fx)
        print('  entry         : %s%s%s%s' % (
            r['entry'] or 'n/a',
            ', code-signed' if r['code_signature'] else '',
            ', TLS' if r['tls'] else '',
            ', ENCRYPTED' if r['encrypted'] else ''))
        if r['rpaths']:
            print('  rpaths        : %s' % ', '.join(r['rpaths']))
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


# --------------------------------------------------------------------------
# Convenience entry points.
#
# These exist because the raw API has been misused three separate times, always
# silently and always producing a confident empty answer:
#
#   - slices() yields (cputype, offset), NOT (offset, size). Unpacking it the
#     other way passes the CPU type to parse() as a file offset, every parse
#     raises, and a caller's `except: continue` swallows the lot.
#   - parse() returns 'dylibs' and 'undefined'. There are no keys named
#     'libraries' or 'imports', so .get() on those returns None and the caller
#     concludes the binary imports nothing.
#
# A scan that returns nothing looks exactly like a finding, which is what makes
# this class of mistake expensive. Use these instead of hand-rolling the loop.


MACHO_MAGICS = (
    bytes.fromhex("cffaedfe"),   # MH_MAGIC_64, little endian
    bytes.fromhex("cafebabe"),   # FAT_MAGIC (universal binary)
    bytes.fromhex("cefaedfe"),   # MH_MAGIC, 32-bit
)


def is_macho(path):
    """True if path is a Mach-O file, by magic rather than by permission bits.

    Extraction from a DMG or a tarball routinely drops the execute bit, so
    testing for it misses most of a corpus.
    """
    try:
        with open(path, "rb") as fh:
            return fh.read(4) in MACHO_MAGICS
    except OSError:
        return False


def scan(path):
    """Every 64-bit slice of one Mach-O, parsed. Returns a list of dicts.

    Each dict is parse()'s output plus 'cputype'. Raises nothing for a
    non-Mach-O - it returns an empty list, so `for s in scan(p)` is safe.
    """
    try:
        data = open(path, 'rb').read()
    except OSError:
        return []
    out = []
    for cputype, base in slices(data):
        try:
            info = parse(data, base)
        except Exception:
            continue
        info['cputype'] = cputype
        out.append(info)
    return out


def walk_machos(root):
    """Yield (path, slice_info) for every Mach-O slice under root.

    Symlinks are skipped: a relocatable bottle is full of links pointing at a
    Cellar prefix that does not exist here, and following them double-counts
    the ones that do resolve.
    """
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            p = os.path.join(dirpath, f)
            if os.path.islink(p) or not is_macho(p):
                continue
            for info in scan(p):
                yield p, info


def imports_by_library(info):
    """{library path: [symbol, ...]} for one slice, from parse()'s 'undefined'."""
    out = {}
    for u in (info.get('undefined') or []):
        out.setdefault(u.get('library') or '', []).append(u.get('symbol'))
    return out


def links_against(info, needle):
    """True if any linked dylib's path contains needle (e.g. 'Cocoa')."""
    return any(needle in str(d) for d in (info.get('dylibs') or [])) or            any(needle in str(d) for d in (info.get('weak_dylibs') or []))



if __name__ == '__main__':
    main()
