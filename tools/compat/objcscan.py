#!/usr/bin/env python3
"""
objcscan - the Objective-C half of compatibility, which symbols cannot measure.

Symbol coverage says whether a binary will *link*. It says nothing about
whether it will *run*, because Objective-C message sends are not symbols: a
binary that links perfectly still dies on the first selector nobody
implements. This reads the ObjC metadata out of a Mach-O directly and reports
the three things that actually decide the outcome:

  subclassed    Which external classes the binary derives from, resolved by
                name through the bind opcodes. These are the expensive ones:
                a class that is merely *messaged* needs the methods that get
                called, but a class that is *derived from* has to behave
                correctly as a base - calling the overrides at the right
                moments, in the right order, with the right state.

                The instance sizes printed below are build-time values, not
                requirements. Under the modern runtime ivars are non-fragile,
                and objc4's reconcileInstanceVariables slides a subclass's
                ivars to sit after whatever the superclass actually turns out
                to be, so our NSView need not match Apple's size. What we
                cannot get away with is different behaviour.

  overrides     For each class the binary defines, the methods it declares.
                Where the superclass is external, these are precisely the
                selectors the framework is expected to call back into - the
                delegate and subclass contract, extracted rather than guessed.

  selectors     Every selector the binary sends, from __objc_selrefs. The
                outer bound on what the frameworks must understand.

Pure Python, like the rest of tools/compat: no otool, no macOS, no VM. It
walks LC_DYLD_INFO bind opcodes itself because the on-disk superclass field of
an externally-rooted class is zero - the name only exists in the bind stream.

  objcscan.py <binary-or-.app> [--json out.json] [--selectors]
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from machoscan import slices, find_binary                    # noqa: E402

LC_SEGMENT_64 = 0x19
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x80000022

BIND_OPCODE_MASK = 0xF0
BIND_IMMEDIATE_MASK = 0x0F
BIND_OPCODE_DONE = 0x00
BIND_OPCODE_SET_DYLIB_ORDINAL_IMM = 0x10
BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB = 0x20
BIND_OPCODE_SET_DYLIB_SPECIAL_IMM = 0x30
BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM = 0x40
BIND_OPCODE_SET_TYPE_IMM = 0x50
BIND_OPCODE_SET_ADDEND_SLEB = 0x60
BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x70
BIND_OPCODE_ADD_ADDR_ULEB = 0x80
BIND_OPCODE_DO_BIND = 0x90
BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB = 0xA0
BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED = 0xB0
BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB = 0xC0

FAST_DATA_MASK = 0x00007FFFFFFFFFF8
SMALL_METHOD_LIST_FLAG = 0x80000000
PTR = 8


def _uleb(data, i):
    result, shift = 0, 0
    while True:
        b = data[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, i
        shift += 7


def _sleb(data, i):
    result, shift = 0, 0
    while True:
        b = data[i]
        i += 1
        result |= (b & 0x7F) << shift
        shift += 7
        if not (b & 0x80):
            if b & 0x40:
                result -= (1 << shift)
            return result, i


class Image:
    """Enough of a Mach-O to resolve virtual addresses and read ObjC metadata."""

    def __init__(self, data, base):
        self.data = data
        self.base = base
        self.segments = []                 # (vmaddr, vmsize, fileoff, filesize)
        self.sections = {}                 # (seg, sect) -> (fileoff, size, vmaddr)
        self.bind_targets = {}             # vmaddr -> symbol name
        self._parse()

    def _parse(self):
        data, base = self.data, self.base
        ncmds, = struct.unpack_from('<I', data, base + 16)
        off = base + 32
        dyld_info = None
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from('<II', data, off)
            if cmd == LC_SEGMENT_64:
                vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', data, off + 24)
                self.segments.append((vmaddr, vmsize, fileoff, filesize))
                nsects, = struct.unpack_from('<I', data, off + 64)
                so = off + 72
                for _ in range(nsects):
                    sect = data[so:so + 16].rstrip(b'\0').decode(errors='replace')
                    seg = data[so + 16:so + 32].rstrip(b'\0').decode(errors='replace')
                    addr, size = struct.unpack_from('<QQ', data, so + 32)
                    foff, = struct.unpack_from('<I', data, so + 48)
                    self.sections[(seg, sect)] = (foff, size, addr)
                    so += 80
            elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
                dyld_info = struct.unpack_from('<8I', data, off + 8)
            off += cmdsize
        if dyld_info:
            # rebase, bind, weak_bind, lazy_bind, export (off,size) pairs
            for o, s in ((dyld_info[2], dyld_info[3]),      # bind
                         (dyld_info[4], dyld_info[5]),      # weak bind
                         (dyld_info[6], dyld_info[7])):     # lazy bind
                if s:
                    self._walk_binds(o, s)

    def _walk_binds(self, offset, size):
        """Interpret bind opcodes into a vmaddr -> symbol map.

        Only the address and symbol are tracked; type, addend and library
        ordinal do not change which class a superclass slot resolves to.

        Every offset update is masked to 64 bits. ADD_ADDR deltas are read as
        uleb128 but are meant to wrap, so a backwards step arrives as a huge
        positive number that dyld truncates on a 64-bit register and Python,
        with unbounded ints, would not.
        """
        data = self.data
        i = self.base + offset
        end = i + size
        seg_index, seg_offset, symbol = 0, 0, None
        mask = 0xFFFFFFFFFFFFFFFF

        def addr():
            if seg_index < len(self.segments):
                return (self.segments[seg_index][0] + seg_offset) & mask
            return None

        while i < end:
            byte = data[i]
            i += 1
            op, imm = byte & BIND_OPCODE_MASK, byte & BIND_IMMEDIATE_MASK
            if op == BIND_OPCODE_DONE:
                continue
            elif op == BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
                pass
            elif op == BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
                _, i = _uleb(data, i)
            elif op == BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
                pass
            elif op == BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
                j = data.index(b'\0', i)
                symbol = data[i:j].decode(errors='replace')
                i = j + 1
            elif op == BIND_OPCODE_SET_TYPE_IMM:
                pass
            elif op == BIND_OPCODE_SET_ADDEND_SLEB:
                _, i = _sleb(data, i)
            elif op == BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
                seg_index = imm
                seg_offset, i = _uleb(data, i)
                seg_offset &= mask
            elif op == BIND_OPCODE_ADD_ADDR_ULEB:
                delta, i = _uleb(data, i)
                seg_offset = (seg_offset + delta) & mask
            elif op == BIND_OPCODE_DO_BIND:
                if symbol and addr() is not None:
                    self.bind_targets[addr()] = symbol
                seg_offset = (seg_offset + PTR) & mask
            elif op == BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
                if symbol and addr() is not None:
                    self.bind_targets[addr()] = symbol
                delta, i = _uleb(data, i)
                seg_offset = (seg_offset + PTR + delta) & mask
            elif op == BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
                if symbol and addr() is not None:
                    self.bind_targets[addr()] = symbol
                seg_offset = (seg_offset + PTR + imm * PTR) & mask
            elif op == BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
                count, i = _uleb(data, i)
                skip, i = _uleb(data, i)
                for _ in range(count):
                    if symbol and addr() is not None:
                        self.bind_targets[addr()] = symbol
                    seg_offset = (seg_offset + PTR + skip) & mask
            else:
                break                      # unknown opcode: stop rather than guess

    def file_offset(self, vmaddr):
        for va, vs, fo, fs in self.segments:
            if va <= vmaddr < va + vs and fs:
                delta = vmaddr - va
                if delta < fs:
                    return self.base + fo + delta
        return None

    def ptr_at(self, vmaddr):
        o = self.file_offset(vmaddr)
        if o is None:
            return None
        try:
            return struct.unpack_from('<Q', self.data, o)[0]
        except struct.error:
            return None

    def cstring(self, vmaddr):
        o = self.file_offset(vmaddr)
        if o is None:
            return None
        try:
            end = self.data.index(b'\0', o)
        except ValueError:
            return None
        return self.data[o:end].decode(errors='replace')


def method_names(img, list_addr):
    """Selector names from a method_list_t, big or small (relative) form."""
    if not list_addr:
        return []
    o = img.file_offset(list_addr)
    if o is None:
        return []
    try:
        entsize_flags, count = struct.unpack_from('<II', img.data, o)
    except struct.error:
        return []
    if count > 8192:                       # not a method list; refuse to guess
        return []
    small = bool(entsize_flags & SMALL_METHOD_LIST_FLAG)
    entsize = entsize_flags & ~SMALL_METHOD_LIST_FLAG
    names = []
    for k in range(count):
        ent = o + 8 + k * entsize
        try:
            if small:
                # name is a relative pointer to a selector *reference*, so it
                # takes one more dereference than the direct form.
                rel, = struct.unpack_from('<i', img.data, ent)
                selref_addr = list_addr + 8 + k * entsize + rel
                sel_ptr = img.ptr_at(selref_addr)
                nm = img.cstring(sel_ptr) if sel_ptr else None
            else:
                name_ptr, = struct.unpack_from('<Q', img.data, ent)
                nm = img.cstring(name_ptr)
        except struct.error:
            break
        if nm:
            names.append(nm)
    return names


def scan(data, base):
    img = Image(data, base)
    out = {'classes': [], 'subclassed_external': {}, 'selectors': []}

    for seg in ('__DATA', '__DATA_CONST', '__DATA_DIRTY'):
        if (seg, '__objc_selrefs') in img.sections:
            foff, size, addr = img.sections[(seg, '__objc_selrefs')]
            for k in range(size // PTR):
                p = img.ptr_at(addr + k * PTR)
                if p:
                    s = img.cstring(p)
                    if s:
                        out['selectors'].append(s)

    classlist = None
    for seg in ('__DATA', '__DATA_CONST', '__DATA_DIRTY'):
        if (seg, '__objc_classlist') in img.sections:
            classlist = img.sections[(seg, '__objc_classlist')]
            break
    if not classlist:
        out['selectors'] = sorted(set(out['selectors']))
        return out

    foff, size, addr = classlist
    for k in range(size // PTR):
        cls_addr = img.ptr_at(addr + k * PTR)
        if not cls_addr:
            continue
        superclass_slot = cls_addr + PTR
        super_ptr = img.ptr_at(superclass_slot)
        data_ptr = img.ptr_at(cls_addr + 4 * PTR)
        if data_ptr is None:
            continue
        ro = img.ptr_at(cls_addr + 4 * PTR)
        ro = (ro or 0) & FAST_DATA_MASK
        ro_off = img.file_offset(ro)
        if ro_off is None:
            continue
        try:
            name_ptr, methods_ptr = struct.unpack_from('<QQ', img.data, ro_off + 24)
            inst_start, inst_size = struct.unpack_from('<II', img.data, ro_off + 4)
        except struct.error:
            continue
        name = img.cstring(name_ptr)
        if not name:
            continue

        # An external superclass is zero on disk; the bind stream holds the name.
        sup = img.bind_targets.get(superclass_slot)
        if sup:
            sup = sup[len('_OBJC_CLASS_$_'):] if sup.startswith('_OBJC_CLASS_$_') else sup
            external = True
        elif super_ptr:
            sup_ro = (img.ptr_at(super_ptr + 4 * PTR) or 0) & FAST_DATA_MASK
            sup_off = img.file_offset(sup_ro)
            sup = None
            if sup_off is not None:
                try:
                    sp, = struct.unpack_from('<Q', img.data, sup_off + 24)
                    sup = img.cstring(sp)
                except struct.error:
                    pass
            external = False
        else:
            external = False

        entry = {'name': name, 'superclass': sup, 'external_superclass': external,
                 'instance_start': inst_start, 'instance_size': inst_size,
                 'methods': method_names(img, methods_ptr)}
        out['classes'].append(entry)
        if external and sup:
            out['subclassed_external'].setdefault(sup, []).append(entry)

    out['selectors'] = sorted(set(out['selectors']))
    out['classes'].sort(key=lambda c: c['name'])
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    target = find_binary(sys.argv[1])
    data = open(target, 'rb').read()

    results = []
    for _cputype, off in slices(data):
        r = scan(data, off)
        r['arch_offset'] = off
        results.append(r)

    r = results[0] if results else {'classes': [], 'subclassed_external': {}, 'selectors': []}
    print('binary: %s' % target)
    print('  ObjC classes defined here : %d' % len(r['classes']))
    print('  external classes subclassed: %d' % len(r['subclassed_external']))
    print('  distinct selectors sent    : %d' % len(r['selectors']))

    if r['subclassed_external']:
        print()
        print('  subclassing these framework classes (the expensive ones - each must')
        print('  behave correctly as a base class, not merely answer messages).')
        print('  Sizes are build-time; non-fragile ivars slide at load:')
        for sup, subs in sorted(r['subclassed_external'].items()):
            print('    %s' % sup)
            for s in subs:
                print('        %-28s ivars %d..%d, %d methods'
                      % (s['name'], s['instance_start'], s['instance_size'],
                         len(s['methods'])))

    overrides = sorted({m for subs in r['subclassed_external'].values()
                        for s in subs for m in s['methods']})
    if overrides:
        print()
        print('  methods it declares on those subclasses (%d) - the callbacks the' % len(overrides))
        print('  frameworks are expected to invoke:')
        for m in overrides:
            print('        %s' % m)

    if '--selectors' in sys.argv:
        print()
        print('  every selector sent (%d):' % len(r['selectors']))
        for s in r['selectors']:
            print('        %s' % s)

    if '--json' in sys.argv:
        out = sys.argv[sys.argv.index('--json') + 1]
        json.dump({'binary': target, 'slices': results}, open(out, 'w'), indent=1)
        print('\nwrote %s' % out)


if __name__ == '__main__':
    main()
