# macOS compatibility: measured status

"Run macOS apps" is not a vibe here. Every number below is produced by
`tools/compat/` from real binaries, is a finite and named work list, and can be
regenerated on any commit. None of the tooling needs macOS, `otool`, `nm`, or a
running VM: it reads Mach-O directly, so coverage can be tracked from CI on
Linux.

```sh
cd tools/compat
python3 coverage.py inventory <openosx-lib-dirs>... -o inventory.json
python3 bundlescan.py /path/to/Some.app -i inventory.json
```

For anything that only imports libSystem, no build is needed at all. The
shipped dylib's export surface *is*
`src/Libraries/libSystem/stub/libSystem.exports`, because that file is passed
to ld64 as `-exported_symbols_list` next to `-dead_strip`, so a symbol missing
from it is discarded from the binary even when its code compiled and linked
fine. Point the inventory straight at it and measure from a source checkout:

```sh
python3 fetchbottles.py lz4 tree gzip --target big_sur -o corpus/
python3 coverage.py inventory ../../src/Libraries/libSystem/stub/libSystem.exports -o inv.json
python3 coverage.py report corpus/lz4/1.9.4/bin/lz4 -i inv.json
```

Always use `bundlescan.py` for real apps. Scanning only `Contents/MacOS/<exe>`
is misleading: VLC's executable is a 62-symbol launcher that dlopens libvlc,
and the actual dependencies live in the bundle's own dylibs. `bundlescan`
walks every Mach-O in the bundle and then **subtracts what the bundle ships
itself**, because an app that carries a library does not need the system to
provide it.

## What OpenOSX exports today

**7,838 symbols and 17 Objective-C classes across 13 libraries** - the image's
libSystem, CoreFoundation, Foundation, libobjc, libc++, Security, IOKit,
CoreServices and SystemConfiguration.

## The corpus, measured

| | The Powder Toy 100.1.400 | VLC 3.0.21 |
|---|---|---|
| Style | SDL2 | hand-written Cocoa |
| Platform gate (`minos`) | 10.13 - passes | 10.7 - passes |
| Mach-O files in bundle | 1 | 100+ |
| Symbols needed in total | 733 | 2,328 |
| Satisfied inside the bundle | 0 | 652 |
| **Required from the system** | **733** | **1,676** |
| **OpenOSX provides** | **546 (74.5%)** | **924 (55.1%)** |
| Still missing | 187 | 752 |
| ObjC selectors sent | **810** | **7,367** |
| ObjC classes subclassed | **7** | **189** |

That table is the strongest argument for the corpus ordering in
[TEST_CORPUS.md](TEST_CORPUS.md). VLC needs **9x the selectors** and
**27x the subclassing** of The Powder Toy. SDL deliberately touches a narrow
slice of Cocoa; VLC is a full Cocoa application. Doing them in that order is
the difference between a reachable milestone and an open-ended one.

### Missing symbols: The Powder Toy (187 measured, 8 since closed)

Of the 14 libSystem imports below, 8 are now exported and verified present in
the built `libSystem.B.dylib`: `_fmodf`, `_wcscasecmp`, `_wcsncasecmp`,
`_wcslcat`, `_wcslcpy`, `_wcsstr`, `___powidf2` and
`__availability_version_check`. The five wide-char sources were in the tree but
in no `target_sources` list; `_fmodf` only wanted an export line; the last two
are new code, because there is no prebuilt `libclang_rt.builtins` for this
cross target.

The six CommonCrypto digests are now closed too, and closing them turned up two
real bugs that a symbol count could never have caught. `CC_MD4`/`CC_MD5`
Init/Update/Final are exported, and **The Powder Toy's libSystem dependency is
fully satisfied** - the `libSystem` line has left its missing-by-library table
entirely. Getting there:

- `CCMD4_STATE_SIZE` was 88 against a 16-byte `ccmd4_initial_state[4]`, so
  `ccdigest_init` over-read 72 bytes. Corrected to 16.
- `CC_MD5` was defined twice in the guest `commoncrypto_static` archive (the
  bridge and `CommonDigest.c`); the bridge is now dropped from the guest and
  kept only for the ld64 self-host build.
- **MD4 computed the wrong digest.** Its round-2 macro `G` was
  `(x & z) | (y & ~z)` - MD5's G - instead of RFC 1320's majority function
  `XY v XZ v YZ`. `F` and `H` are shared between the two, so only round 2 was
  wrong, giving plausible-but-incorrect output. It survived because nothing in
  the tree called MD4 until this import. Verified: MD4/MD5/SHA1 now match the
  RFC 1320/1321 and FIPS 180 vectors through the real `ccdigest()` driver
  before the symbols went on the export list.

Powder Toy coverage is now **76.3%**, and everything left is framework work:
AppKit 48, Security 42 (a TLS stack), CoreGraphics 38, then the Foundation and
CoreFoundation ObjC classes and small CoreVideo/Metal/Carbon/IOKit tails. None
of it is a libSystem function any more.

The table below is the original measurement, kept as the baseline.

| Library | Missing | Kind |
|---|---|---|
| AppKit | 48 | structural |
| Security | 42 | incremental (Secure Transport TLS) |
| CoreGraphics | 38 | structural |
| libSystem | 14 | incremental (CommonCrypto digests, `__powidf2`) |
| Foundation | 14 | incremental |
| CoreVideo | 7 | structural (`CVDisplayLink*`, vsync timing) |
| Metal | 7 | likely optional; SDL probes and falls back |
| CoreFoundation | 7 | incremental |
| Carbon | 5 | legacy event/keyboard bits |
| IOKit | 3 | incremental |
| CoreServices | 2 | incremental |

### Missing symbols: VLC (752, top of the list)

AppKit 166, ApplicationServices 98, Foundation 86, libSystem 52, Security 49,
CoreVideo 48, CoreServices 40, OpenGL 39, CoreFoundation 26, then the media
stack: AudioToolbox 18, AVFoundation 17, VideoToolbox 15, AudioUnit 14,
MediaPlayer 13, CoreMedia 12, QuartzCore 12, CoreAudio 11.

The media frameworks are a whole second project beyond the GUI stack, which is
why VLC sits at Tier 2 and "plays a video" is a much later milestone than
"draws a window".

## How to read these numbers honestly

**Symbol coverage is the optimistic axis.** Objective-C message sends are not
symbols. The Powder Toy would *link* with 187 more symbols and then die on the
first unimplemented selector out of 810. VLC sends 7,367. The `superrefs`
count is the expensive one: subclassing `NSView` requires a compatible ivar
layout and correct dispatch ordering, not merely the right method names.

**The gap splits into two very different kinds of work:**

- **Incremental** - libraries we already build, where each missing symbol is a
  function to write: libSystem, Foundation, CoreFoundation, IOKit,
  CoreServices, Security. For The Powder Toy that is 82 of the 187.
- **Structural** - AppKit, CoreGraphics, CoreVideo, ApplicationServices,
  QuartzCore. These need the stack in [AQUA_UI_PLAN.md](AQUA_UI_PLAN.md), and
  that is the multi-year part. None of the five exists anywhere in the tree.

**The "incremental" label is doing too much work for Foundation.**
CoreFoundation is genuine Apple code, the `swift-corelibs-foundation` fork of
CF, vendored nearly whole: 95 of its 98 sources compile, including all of
CFString, CFArray, CFDictionary, CFBundle, CFPreferences, CFRunLoop and
CFPropertyList. Foundation is not that. It is a from-scratch reimplementation
of **6 `.m` files, 443 lines and 8 classes** (NSString, NSCFString, NSArray,
NSMutableArray, NSDictionary, NSMutableDictionary, NSURL, NSError), each a thin
forwarding shim over the corresponding CF type. NSException, NSInvocation,
NSNotificationCenter and NSFileManager do not exist.

Toll-free bridging is also only half-built, and the missing half is the one
apps use. CF objects get an ObjC `isa` stamped at creation so they can receive
messages, but the reverse path is compiled out: `CF_OBJC_FUNCDISPATCHV` and
`CF_OBJC_CALLV` expand to `do { } while (0)` and `(0)`, which disables all 231
dispatch sites across 18 CF files. So a CF function handed an ObjC subclass
will not call back into it.

Read that against the Foundation numbers in the table above (86 missing symbols
for VLC) and the split changes character: some of it really is one function per
symbol over working CF, and the rest is new subsystem work that happens to be
spelled with a Foundation prefix.

## Tier 0: reached. Unmodified macOS binaries run on OpenOSX

On 2026-08-09, three Homebrew x86_64 bottles executed on a booted OpenOSX
image. Nothing about them was recompiled, relinked or patched; they are the
exact Mach-O files Homebrew publishes, and unlike everything else in the image
they were never built by this project's toolchain nor linked against this
libSystem.

```
# /opt/compat-test/tree --version
tree v2.1.1 (c) 1996 - 2023 by Steve Baker, Thomas Moore, Francesc Rocher, ...

# /opt/compat-test/tree -L 1 /
/
|-- System
|-- bin
...
16 directories, 1 file

# /opt/compat-test/lz4 --version
*** LZ4 command line interface 64-bits v1.9.4, by Yann Collet ***

# /opt/compat-test/gzip --version
gzip 1.13
```

`tree` did not merely start: it walked a real filesystem and printed it. The
only dyld output on the console was `dyld: setting comm page to 0x800000000`,
which is informational. No missing library, no unresolved symbol, no abort.

That exercises, in order: the LC_BUILD_VERSION platform gate, dylib
resolution, two-level namespace symbol binding, classic bind-opcode fixups,
dyld startup, and the syscall layer - against a program that knows nothing
about us.

Reproduce with `tools/compat/fetchbottles.py` to populate a directory, then
point `OPENOSX_COMPAT_CORPUS` at it and build `.#image-minimal --impure`.

Two notes on how it was run, both of which cost time to learn:

- **Use the TCG runner on an AMD host.** `.#kvm-runner` passes `-cpu host`,
  which hands XNU an AuthenticAMD CPU; the kernel dies immediately after
  handoff with the serial log stopping dead at `entry = 0x...`. `.#vm-runner`
  passes `-cpu IvyBridge,vendor=GenuineIntel`, which is the masquerade XNU
  needs. Any future KVM-on-AMD attempt also needs
  `/sys/module/kvm/parameters/ignore_msrs` set to `Y`.
- The minimal image has no `od`, and no networking (`en0 did not appear`).
  Neither matters here, but both will confuse an unprepared test script.

## What it took: every measured CLI binary links

Eleven unmodified Homebrew x86_64 bottles, fetched with `fetchbottles.py` and
diffed against `libSystem.exports`:

| Binary | libSystem imports | Missing | Self-contained? |
|---|---|---|---|
| lz4 1.9.4 | 59 | **0** | yes |
| tree 2.1.1 | 62 | **0** | yes |
| gzip 1.13 | 92 | **0** | yes |
| xz 5.4.4 | 66 | **0** | bundles liblzma |
| zstd 1.5.5 | 82 | **0** | bundles libzstd, liblz4, liblzma |
| jq 1.7 | 133 | **0** | bundles libonig |
| nano 7.2 | 156 | **0** | bundles libncursesw, libintl |
| lzmadec / lzmainfo / xzdec | 22 each | **0** | bundle liblzma |
| pzstd | 57 | **0** | bundles libzstd, libc++ |

Getting there took seven export lines and one 30-line source file, because
almost everything was already compiled and merely unexported. `_lgamma_r`,
`_lgammaf_r`, `_frexpl`, `_jn`, `_jnf`, `_yn`, `_ynf` and `_timegm` were sitting
in the link (openlibm is `-force_load`ed) and being dead-stripped for want of an
export line; `_fdatasync` is a generated syscall stub (`syscalls.master:278`)
in the same position. Only `_scalb` was genuinely absent, openlibm having
dropped the obsolete SVID spelling, and it is now
`libc/libm/pd_libm_scalb.c` alongside the other functions openlibm omits.

**lz4, tree and gzip are the Tier 0 targets**: single-dylib, no bundled
dependencies, no code signature, no TLS. Running one exercises the platform
gate, dylib resolution, two-level namespace binding, dyld startup and the
syscall layer against a binary that knows nothing about us.

The remaining risk is not symbols. It is that no genuinely foreign
dynamically-linked binary has ever been executed on OpenOSX: every userland
component, zsh and Python and XFCE included, is cross-built by this project's
own toolchain and linked against this libSystem, which turns a missing export
into a build-time link error rather than a runtime failure. A Homebrew bottle
would be the first program to arrive with expectations we did not get to
negotiate.

## The desktop boots

The full `.#image` builds and boots to a painted XFCE desktop: XNU 20.5.0 ->
ext4 root -> launchd -> `pd-console-login` -> `startx` -> Xorg on the real
1280x800 GOP framebuffer -> xfwm4, xfce4-panel and xfdesktop, with the Dawn
wallpaper as the seeded backdrop. Under TCG it reaches the bare X server in
about two minutes and finishes painting the desktop around four; the frames are
stable from six minutes on. Reproduce with
`tools/testing/boot-desktop-screenshot.sh`.

Getting there took five distinct real failures, each found by building cleanly
and reading the leaf error rather than the propagation:

1. **Build parallelism.** `max-jobs = auto` with `cores = 0` is 12 jobs each
   using all 12 cores - roughly 144 concurrent compilers. It exhausted the VM
   and killed WSL twice, and those hard terminations corrupted the nix store.
   Capped to `max-jobs = 6`, `cores = 2`.
2. **dbus setuid.** `meson_post_install.py` chmods `S_ISUID` on
   `dbus-daemon-launch-helper`; WSL2's ext4 refuses the setuid bit even as root,
   so the chmod raised PermissionError and aborted the whole meson install,
   taking all of XFCE with it. The bit is meaningless here (the guest runs as
   root), so it is dropped in postPatch.
3. **Store corruption.** glib's output and several source archives had been
   truncated by the crashes. `nix-store --verify --check-contents --repair`
   fixed most; glib needed `--repair-path` specifically, because a GC root held
   it and `--delete --ignore-liveness` would not remove it.
4. **`_fdatasync`.** Exporting it (for a Homebrew bottle) made cross-compiled
   packages' `has_function('fdatasync')` link check pass, so xfconf took its
   Linux path and then failed to compile against the Apple header, which never
   declares it. macOS has no fdatasync; exporting a Linux-ism it lacks does more
   harm than good, so it was removed.
5. **`-lpthread` / `-lz` / `-liconv` in dillo.** OpenOSX folds pthread into
   libSystem and ships no standalone libpthread, and dillo had no zlib or iconv
   on its link path at all while its Makefile appended all three.

## Three frameworks brought up: CoreGraphics, CoreVideo, Metal

The Powder Toy is now at **83.5%** symbol coverage (from 74.5%), because three
frameworks it depends on now build and export what it imports. Measured against
a complete 49-library inventory; each framework's line has left the
missing-by-library table entirely.

| Framework | install name | closes | how |
|---|---|---|---|
| CoreGraphics | `.../CoreGraphics.framework/.../CoreGraphics` | 38 | geometry math, CGColor/CGColorSpace CFTypes, CGDirectDisplay over PDGOP |
| CoreVideo | `.../CoreVideo.framework/.../CoreVideo` | 7 | CVDisplayLink as a 60 Hz timer thread |
| Metal | `.../Metal.framework/.../Metal` | 7 | nil-device probe stub + 5 empty descriptor classes |

None are stubs where it counts. CoreGraphics reports the *real* framebuffer
geometry through PDGOP (`CGDisplayBounds`, the display mode), its out-of-line
geometry math passed 18/18 known-value checks before it shipped, and CGColor is
a genuine CFType registered against the shipped CoreFoundation with the same
`_CFRuntimeRegisterClass` skeleton `DiskArbitration/DADisk.c` uses. CoreVideo's
display link fires a real `CVTimeStamp` from `mach_absolute_time` and drops
frames rather than spiralling when a callback overruns a period - the correct
behaviour precisely where OpenOSX is slow. Metal returns nil from
`MTLCreateSystemDefaultDevice`, which is the documented "no Metal here" signal
that makes SDL2 fall back to OpenGL.

Building each one (not just writing it) caught a real SDK-header subtlety every
time: CoreGraphics collided with the five static-inline shadows the SDK macro's
to `__`-prefixed functions (`CGAffineTransformMake`, `CGPointApplyAffineTransform`,
`CGSizeApplyAffineTransform`, `CGPointEqualToPoint`, `CGSizeEqualToSize` - the
complete set, confirmed by grepping the extracted headers), and CoreVideo's
`CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext` had to match the SDK's exact
`CGLContextObj`/`CGLPixelFormatObj` signature.

What remains for The Powder Toy to *link*: AppKit 48, Security 42, Foundation
14, CoreFoundation 7, Carbon 5, IOKit 3, CoreServices 2. Linking is necessary
but not sufficient - AppKit also has to *be* a window server (the Cocotron
structural core in [AQUA_UI_PLAN.md](AQUA_UI_PLAN.md)), and Security's 42 are the
Secure Transport TLS surface, a wrapper over the already-built OpenSSL that
needs live-handshake testing before it can be trusted.

## The third axis, finally measured: what SDL2 asks of Cocoa

`objcscan.py` reads ObjC class metadata and the `LC_DYLD_INFO` bind opcodes
straight out of a Mach-O, which turns "810 selectors, 7 superrefs" from a
number into a named work list. Run against `libSDL2-2.0.0.dylib` (the Homebrew
`sdl2` 2.28.3 big_sur bottle, which is the exact Cocoa layer The Powder Toy
runs on; full output in [compat-sdl2-objc.json](compat-sdl2-objc.json)):

| Base class | Subclasses | Methods declared on them |
|---|---|---|
| NSResponder | 1 (`Cocoa_WindowListener`) | **58** |
| NSView | 3 (`SDLView`, `SDLTranslatorResponder`, `SDL_cocoametalview`) | 33 |
| NSOpenGLContext | 1 (`SDLOpenGLContext`) | 12 |
| NSWindow | 1 (`SDLWindow`) | 9 |
| NSApplication | 1 (`SDLApplication`) | 2 |
| NSObject | 10 (data holders) | 142 |

656 distinct selectors sent, 133 of them Cocoa-shaped. SDL2 references 48
framework classes in total, and `machoscan` attributes each to the framework it
comes from: AppKit 48 symbols, CoreGraphics 38, Foundation 15, CoreVideo 7,
Metal 7, Carbon 5.

Two things fall out of this that were not previously obvious.

**The real cost is being a base class, not answering messages.** Ten of the
seventeen classes SDL2 defines derive from NSObject and are just data holders;
those cost nothing. The five that derive from NSResponder, NSView, NSWindow,
NSOpenGLContext and NSApplication are the whole problem, because a base class
has to call its overrides at the right moments and in the right order. The 58
methods on `Cocoa_WindowListener` are the event and window-state contract, and
they must be *invoked*, not merely accepted.

**Ivar layout is not the obstacle it was assumed to be.** Earlier notes here
treated superrefs as expensive partly because subclassing needs a compatible
ivar layout. Under the modern runtime it does not: ivars are non-fragile, and
objc4's `reconcileInstanceVariables` slides a subclass's ivars to sit after
whatever the superclass turns out to be at load time. Our NSView does not have
to match Apple's instance size. It has to match Apple's behaviour.

Metal is the one dependency worth calling out as optional: SDL2 probes for it
and falls back, so `METAL_RenderData` and `METAL_TextureData` can go
unimplemented without blocking a window.

## Corrections to earlier versions of this document

Two claims that stood here were wrong, and both mattered:

**"CommonCrypto is a linkage gap fixed in an afternoon."** Right that it is a
linkage gap, wrong about the mechanism, and wrong about the cost. The
commented-out `add_darwin_circular_library` blocks in
`src/Libraries/CommonCrypto/CMakeLists.txt` and `libSystem/corecrypto/` are
dead upstream scaffolding: they name ten sibling targets, nine of which have
never existed in this tree, and `cmake/circular.cmake` has never once been
executed here. Enabling them is neither necessary nor sufficient.
`commoncrypto_static` is already linked into the shipped libSystem and
`CommonDigest.c` already defines CC_MD4/MD5/SHA1 via its `DIGEST_SHIMS` macro.
The real gate is the same export allow-list as everything else. Two hazards
still block a one-line-per-symbol fix: `libcn/pd_cc_digest_bridge.c`
independently defines all four `CC_MD5_*` symbols in the same archive, so
force-referencing them can produce a duplicate-symbol failure; and
`ccdigest_init` copies `di->state_size` (88) bytes out of the 16-byte
`ccmd4_initial_state`, a 72-byte over-read that should be fixed before MD4 is
exported. `__powidf2` is unrelated to any of this: there is no compiler-rt in
the tree, and the established pattern is a hand-written builtin in
`pd_libSystem_compat.c`, which already carries `__udivti3` and friends.

**"dyld refuses a binary whose minimum OS is newer than the running system."**
It does not. `MachOFile::builtForPlatform`
(`src/Libraries/dyld/upstream/dyld3/MachOFile.cpp:482`) takes `minOS` and `sdk`
as block parameters and then compares only the platform ID, and
`loadableIntoProcess` is built on it. There is no version ceiling anywhere on
the load path. Measured accordingly: Monterey bottles of tree, lz4 and jq
(`minos 12.0`) also come out at zero missing symbols. Prefer older targets
because they import less, not because newer ones are refused.

## What dyld actually is, and why it is not the blocker

`src/Libraries/dyld` is Apple's real dyld-832 (macOS 11.4), not a
reimplementation, and it is built and shipped. Verified present and wired:
two-level namespace ordinals, `dlopen`/`dlsym`/`dlclose`, weak imports and weak
dylibs, `@rpath`/`@loader_path`/`@executable_path`, and chained fixups
(`DYLD_CHAINED_PTR_64` and `_64_OFFSET` are the two formats compiled on
x86_64). AMFI is stubbed fully permissive. The shared cache is optional, which
forces the dyld2 loose-dylib-on-disk path, which is exactly what OpenOSX needs.

Every bottle measured above uses classic bind opcodes rather than chained
fixups, so that support is headroom rather than a dependency. `machoscan.py`
now reports the fixup format, entry style, rpaths, code signature and TLS use,
because those gates precede symbols entirely: a binary whose fixup format the
loader does not implement cannot load no matter how complete the exports are.
