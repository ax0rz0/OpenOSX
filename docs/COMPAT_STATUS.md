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

### Missing symbols: The Powder Toy (187)

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
  that is the multi-year part.

## The cheapest real milestone

`CC_MD4_*`, `CC_MD5_*` and `__powidf2` are missing from libSystem even though
CommonCrypto is in the tree. That smells like a linkage or re-export gap rather
than absent code, and it is the sort of thing fixed in an afternoon.

More broadly: closing the ~82 incremental symbols would not run a GUI app, but
it would take a **Tier 0 command-line binary** from nearly-running to running,
which validates the entire dyld and loader path against a real third-party
Mach-O. That is a far nearer and more informative milestone than anything in
the GUI stack, and it is the recommended next step.
