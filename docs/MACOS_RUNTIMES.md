# Running macOS language runtimes on OpenOSX

The goal here is the *macOS* build of Python, Java, Node, Ruby, Perl, Lua and
Tcl/Tk — the binaries a Mac would actually run — not source ports rebuilt with
our toolchain. A source port proves our compiler works. An unmodified macOS
binary proves *OpenOSX* works.

Supply is Homebrew's own bottles, pulled straight from ghcr.io with
`tools/compat/fetchbottles.py`. No Mac, no `brew` client, nothing built here.

```bash
python3 tools/compat/fetchbottles.py python@3.11 openjdk node ruby perl lua tcl-tk \
    --target big_sur -o corpus/
```

## What was measured

326 Mach-O files across seven runtimes, scored against everything OpenOSX
exports today (libSystem, CoreFoundation, libobjc, CoreGraphics, CoreVideo,
Metal, Security) *plus each runtime's own bundled dylibs* — because
`libjli.dylib`, `libruby.3.2.dylib` and `libperl.dylib` ship inside the bottle
and are not ours to provide.

Getting that second part wrong makes the numbers meaningless. A first pass
without it reported Java at 18% and Perl at 14%, which reads like a mountain of
missing kernel surface; almost all of it was each runtime failing to find its
own library.

### Launchers

| runtime | coverage | still missing |
|---|---|---|
| openjdk 21 | **100%** | – |
| ruby 3.2.2 | **100%** | – |
| perl 5.36.1 | **100%** | – |
| tcl-tk 8.6.13 | **100%** | – |
| python 3.11.5 | 95.2% | 1 |
| lua 5.4.6 | 94.5% | 3 (`readline`, from a formula we did not fetch) |
| node 20.7.0 | 16.7% | bundles its own OpenSSL/ICU/libuv, none fetched |

### The libraries behind them

This is where the real surface is, and it is all libSystem:

| library | coverage | missing |
|---|---|---|
| `libjvm.dylib` | 96.5% | 8 |
| `Python` (framework) | 93.1% | 22 |
| `libruby.3.2.dylib` | 92.1% | 29 |
| `libperl.dylib` | 89.5% | 27 |

## The work list

**135 distinct libSystem symbols**, of which **8 were already implemented** and
merely unexported (now fixed in `libSystem.exports`). The remaining ~120 fall
into a handful of coherent groups rather than a long tail:

| group | count | notes |
|---|---|---|
| POSIX/SysV IPC — `sem_*`, `msg*`, `sem*`, `ftok` | 14 | python, perl |
| `dbm_*` (ndbm) | 9 | python, perl |
| BSD netdb enumeration — `endhostent`, `getnetent`, … | 15 | perl |
| dyld introspection — `_dyld_image_count`, `_dyld_get_image_header`, … | 5 | node, openjdk, ruby |
| legacy `NS*` loader API — `NSLinkModule`, `NSLookupAndBindSymbol`, … | 10 | tcl-tk, python |
| CommonCrypto SHA1/384/512 | 9 | ruby — same shape as the MD5 already in tree |
| `unw_*` (libunwind low-level) | 6 | ruby |
| thread-locals — `__tlv_bootstrap`, `__tlv_atexit` | 2 | node, openjdk, ruby |
| `$INODE64` aliases — `seekdir`, `telldir`, `fts_*` | 5 | node, perl, ruby, tcl-tk |
| Mach VM — `mach_vm_remap`, `vm_read`, `vm_region_recurse_64`, … | 6 | node, openjdk, ruby |
| assorted libc — `mkfifo`, `clock_getres`, `lchmod`, `forkpty`, `waitid`, … | ~30 | spread |
| Sun RPC / NIS — `xdr_*`, `yp_*`, `clnt_*` | 9 | python |

Two observations worth keeping:

- **The `$INODE64` group is aliasing, not implementation.** macOS renamed the
  64-bit-inode variants at the symbol level; we have `seekdir`, we just do not
  publish `seekdir$INODE64`.
- **Sun RPC and NIS are the least worth doing.** They are legacy python module
  dependencies, deprecated on macOS itself, and nothing an actual script
  reaches. They are listed for completeness, not as a target.

## Reproducing

```bash
python3 tools/compat/coverage.py inventory \
    src/Libraries/libSystem/stub/libSystem.exports \
    src/Libraries/Security/Security.exports \
    <built frameworks> corpus/ -o inv.json
python3 tools/compat/coverage.py report corpus/ruby/*/lib/libruby.3.2.dylib -i inv.json
```
