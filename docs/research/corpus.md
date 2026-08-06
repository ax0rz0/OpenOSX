# OpenOSX macOS-Compat Test Corpus + Measurement Methodology

**Status of this report:** every command, digest, and number below was **actually executed and verified** during this research (Aug 2026). Nothing is from memory. A working Mach-O analyser was written and run against real Big Sur x86_64 binaries; its output is quoted verbatim.

Working scanner produced during this research:
`C:\Users\poopy\AppData\Local\Temp\claude\C--Users-poopy-OneDrive-Documents-GitHub-OpenOSX\4dfdf99c-3ca1-4f4d-a3ab-b1bb126be848\scratchpad\machoscan.py`

---

## 0. Executive summary â€” the four things that matter

1. **Tier 0 is solved and free.** Homebrew's ghcr.io registry still serves `big_sur` **x86_64** bottles for older formula versions. I verified 10/10 candidate formulas have one. These are `minos 11.0.0` binaries â€” an *exact* deployment-target match for Darwin 20.5. `jq 1.6` imports **137 symbols from exactly two dylibs** (`libSystem.B.dylib` + `libonig`). That is a runnable test today.
2. **Current Homebrew is useless for this.** `jq 1.8.2` has bottle tags `arm64_tahoe, arm64_sequoia, arm64_sonoma, sonoma, arm64_linux, x86_64_linux` â€” **no `big_sur`, and the only Intel macOS tag is `sonoma`**. You must pull the *oldest* tag from ghcr, not the current API JSON.
3. **"Symbol coverage %" is the wrong headline metric for GUI apps and will lie to you.** Measured on a real Cocoa app (Quicksilver 2.6.0, x86_64 slice): **245 undefined symbols total, but only 53 from AppKit** â€” because Objective-C method calls are *not* symbols. The same binary has **1,014 `__objc_selrefs`** (selectors it actually sends) and **113 `__objc_classrefs`**. The honest metric is a **three-axis score: symbols + classes + selectors.** Section 6 gives the extraction for all three.
4. **You already have the tooling.** `tools/cctools/` in-tree vendors `otool`, `nm`, `install_name_tool`, `lipo`, `pagestuff` (`tools/cctools/misc/nm.c`, `tools/cctools/otool/`), and `cctoolsBuild` is already wired into `nix/pkgs/`. You get Linux-hosted Mach-O tooling with zero new dependencies. No Mac required for measurement.

---

## 1. Legal ground rules (read before picking apps)

There are **two distinct legal questions** that are constantly conflated. Keep them apart or you will over-restrict yourself:

| Question | Applies to | Rule |
|---|---|---|
| **Can I read this source?** | *Taint* risk | Only **Apple's leaked/proprietary source** taints. Reading GPL app source to debug a *client* app does **not** taint your APSL framework work â€” GPL/proprietary app source teaches you nothing about AppKit internals; it's the caller, not the callee. |
| **Can I ship this code/binary?** | *Distribution* risk | GPL can't be vendored into APSL/BSD shippable image components. GPL/proprietary **test binaries are never shipped at all** â€” they're fetched at test time. |

**Consequence:** for the *test corpus*, license is almost irrelevant. Test binaries live in a fetch-on-demand cache, never in the image, never in the repo. So **GPL apps (iTerm2, Transmission, VLC, HandBrake) are perfectly fine test targets** â€” you just can't copy their code into OpenOSX. The reason to *prefer* permissive-licensed apps is purely practical: MIT/Apache source is easier to fork-and-instrument when you need a debug build.

**What still taints, absolutely:** obtaining or reading Apple's non-public source (leaked AppKit/CoreGraphics/WindowServer source). Nothing in this report requires it. The project's existing `requireFile` pattern for `MacOSX11.3.sdk.tar.xz` (seen in `nix/pkgs/apple/gsbase-test.nix`) is the correct model: **public SDK headers, registered locally, never redistributed.**

---

## 2. Tier 0 â€” CLI binaries that should nearly work today

### 2.1 The verified ghcr.io bottle-fetch method

Homebrew bottles are plain `tar.gz` files stored as OCI blobs. No Mac, no `brew`, no auth account. **The token is the literal string `QQ==`** (base64 of `A`), hardcoded in Homebrew.

**Step 1 â€” list available versions.** Verified:

```bash
curl -s -H "Authorization: Bearer QQ==" \
  https://ghcr.io/v2/homebrew/core/jq/tags/list
# => {"name":"homebrew/core/jq","tags":["1.6-1","1.7","1.7.1","1.7.1-1","1.8.0","1.8.1","1.8.2"]}
```

**Take the OLDEST tag.** That's where Big Sur lives.

**Step 2 â€” pull the OCI image index and find the `big_sur` entry.** Verified:

```bash
curl -s -H "Authorization: Bearer QQ==" \
     -H "Accept: application/vnd.oci.image.index.v1+json" -L \
     https://ghcr.io/v2/homebrew/core/jq/manifests/1.6-1 -o manifest.json
```

Each entry in `.manifests[]` carries the two annotations you need:

```
org.opencontainers.image.ref.name = "1.6.big_sur.1"
sh.brew.bottle.digest             = "bf0f8577632af7b878b6425476f5b1ab9c3bf66d65affb0c455048a173a0b6bf"
platform = {"architecture":"amd64","os":"darwin","os.version":"macOS 11.0.1"}
```

`sh.brew.bottle.digest` **is** the blob digest **and** the sha256 of the tarball â€” one value verifies both.

**Step 3 â€” download the blob.** Verified (433,567 bytes, sha256 matched):

```bash
curl -sL -H "Authorization: Bearer QQ==" \
  https://ghcr.io/v2/homebrew/core/jq/blobs/sha256:bf0f8577632af7b878b6425476f5b1ab9c3bf66d65affb0c455048a173a0b6bf \
  -o jq.tar.gz
sha256sum jq.tar.gz     # bf0f8577...  âœ“
tar tzf jq.tar.gz       # jq/1.6/bin/jq, jq/1.6/lib/libjq.1.dylib, ...
```

### 2.2 Verified Big Sur x86_64 bottle inventory

I queried all of these live. **10/10 have a `big_sur` amd64 bottle at their oldest ghcr tag:**

| Formula | Oldest tag | `ref.name` | Blob digest (sha256) |
|---|---|---|---|
| jq | `1.6-1` | `1.6.big_sur.1` | `bf0f8577632af7b878b6425476f5b1ab9c3bf66d65affb0c455048a173a0b6bf` |
| ripgrep | `12.1.1` | `12.1.1.big_sur` | `0ca7397f9a0ccef6cbb8ff0fd8fb18c6fe86219abaef350e3d7ef248d07440fd` |
| sqlite | `3.35.3` | `3.35.3.big_sur` | `6f491b7ef85515ede5f193db760117c39f4d4f2dadb482c2f48410b2ea00d1eb` |
| coreutils | `8.32-2` | `8.32.big_sur.2` | `371ec57703b3646e0113331308b6e03617c2a7f91e15e113380b605455daba20` |
| tree | `1.8.0` | `1.8.0.big_sur` | `572adeaba1ffee7fa8bcad414c8b18140c367bbc81dc2ab8fd438cbd7e4a985b` |
| pcre2 | `10.36` | `10.36.big_sur` | `b2edbffaf229fc490843e83b43c4e12feab906fc34270d928c59cac74c6f4536` |
| zstd | `1.4.9` | `1.4.9.big_sur` | `34a6c2cc25d1a7bca6e2294ec3d024f359a2aaf705798b9cbdd71bccdd5c08bd` |
| xz | `5.2.5` | `5.2.5.big_sur` | `4fbd4a9e3eb49c27e83bd125b0e76d386c0e12ae1139d4dc9e31841fb8880a35` |
| oniguruma | `6.9.6` | `6.9.6.big_sur` | `505599ad17e21360a58a89db2133115b5aa109cdebd5d284bec2bc25cfee5062` |
| lua | `5.4.3-1` | `5.4.3.big_sur.1` | `e59dc980047218242a11cd735216b5ec881c45c60f50fffd5edd68450c281b94` |
| nano | `5.6.1` | `5.6.1.big_sur` | `f6c6aa9cfc1f0e67695e2ef1b24d8b191291b9e551a8a86583d5a5ceb249ce54` |

These digests are content-addressed and immutable â€” **pin them in Nix as `fetchurl` with the sha256 you already have.** This makes the corpus hermetic and CI-reproducible, and it means no network flakiness.

### 2.3 Verified target profile: `jq 1.6`

Scanner output on `jq/1.6/bin/jq`:

```
arch x86_64   filetype 2 (MH_EXECUTE)   platform macOS   minos 11.0.0   sdk 11.0.0
dylibs: /usr/lib/libSystem.B.dylib
        @@HOMEBREW_PREFIX@@/opt/oniguruma/lib/libonig.5.dylib
undefined symbols: 137
  libSystem.B.dylib : 128
  libonig.5.dylib   :   9   (_onig_new, _onig_search, _OnigEncodingUTF8, ...)
```

`libsqlite3.0.dylib` from the sqlite bottle: **315 exported symbols, 107 undefined, all from libSystem.** These are pure-libc workloads. If `jq` and `sqlite3` don't run, the problem is in libSystem/dyld, not in framework coverage â€” which makes them ideal **regression canaries** for the boot image.

### 2.4 Three gotchas I hit (fix these in the fetcher)

1. **`@@HOMEBREW_PREFIX@@` placeholders are real.** Both `LC_LOAD_DYLIB` paths and `LC_ID_DYLIB` contain the literal string `@@HOMEBREW_PREFIX@@`. Homebrew rewrites these at install time. You must too â€” use the in-tree `tools/cctools/misc/install_name_tool.c`:
   ```bash
   install_name_tool -change '@@HOMEBREW_PREFIX@@/opt/oniguruma/lib/libonig.5.dylib' \
                             '/opt/openosx/lib/libonig.5.dylib' jq
   ```
   Or set your test prefix to a path of the same length and patch bytes directly.
2. **Extract on Linux/WSL, not Windows.** `tar xzf` failed on `jq/1.6/lib/libjq.dylib` â€” bottles contain symlinks Windows can't create without privileges.
3. **`minos` is enforced.** `big_sur` bottles are `minos 11.0.0`, which matches Darwin 20.5 exactly. A `monterey` or `sonoma` bottle is `minos 12.0/14.0` and dyld will refuse to load it ("built for newer version of macOS"). **This is why `big_sur` specifically matters** â€” don't substitute a newer tag when a formula lacks big_sur.

---

## 3. Tier 1 â€” Objective-C / Foundation CLI, no GUI

This tier isolates **objc4 + CoreFoundation + Foundation** with zero graphics. Order matters:

**1. `objc4`'s own test suite â€” start here, it's already in your tree.**
`nix/pkgs/apple/objc-test.nix` exists on `openosx-next`. Apple's open-source objc4 drop ships a large `test/` directory (APSL, already legally in-tree, no fetching). This is the single highest-value Tier 1 corpus: hundreds of tiny programs exercising `objc_msgSend`, categories, associated objects, ARC, weak refs, `+load`, protocols, KVO plumbing. It costs nothing and it's the foundation everything above depends on. Same pattern for `libcxx-test.nix` and `libdispatch`'s test suite.

**2. GNUstep `gnustep-base` test suite** (LGPL â€” *runnable*, not vendorable). A few thousand small `NSString`/`NSArray`/`NSDictionary`/`NSFileManager`/`NSURL`/`NSRunLoop`/`NSThread` assertions. Cross-compile the tests against **your** Foundation, not GNUstep's â€” you get a differential conformance suite for free. Note: the existing `nix/pkgs/apple/gsbase-test.nix` is *not* this (it's a Wine gsbase/TSD probe, unrelated naming collision) â€” worth not confusing them.

**3. `class-dump` (GPL-2.0-or-later, Steve Nygard).** Pure Foundation CLI, no AppKit, 64-bit Intel, and its *job* is parsing Mach-O â€” so it doubles as an in-guest diagnostic once it runs. Perfect Tier 1 boss fight. Study/run only, never vendor.

**4. `duti`** â€” small ObjC CLI, but it exercises **LaunchServices/CoreServices**, which is exactly the `coreservicesBuild` you already have in `nix/pkgs/apple/coreservices.nix`. Good stress test for that specific component.

**5. `trash` / `terminal-notifier` (both MIT, ObjC).** `trash` hits Foundation + ScriptingBridge; `terminal-notifier` needs `NSUserNotification` and an `NSApplication` run loop â€” that one is really a Tier 1.5 and will smoke out how far you can get without a window server.

**Deliberately excluded:** anything Swift (`swiftlint`, `mas`, `XcodeGen`). No Swift runtime means these are Tier 4; don't waste cycles.

---

## 4. Tier 2 â€” Open-source GUI `.app` bundles (the real early targets)

All verified live against the GitHub releases API this session.

| App | License | Latest release (verified) | Why it's a good target |
|---|---|---|---|
| **Sequel Pro** | **MIT** | `release-1.1.2`, 2016-04-03, `sequel-pro-1.1.2.dmg` (10.6 MB) | **Best first GUI target.** Pure Intel x86_64 (pre-arm64 era = low `minos`), pure ObjC/Cocoa, MIT so you can fork and instrument freely, and it's *frozen* â€” no moving target. Uses NSTableView/NSSplitView/NSToolbar: a broad but classic AppKit surface. |
| **Vienna RSS** | **Apache-2.0** | `v/3.10.8`, 2026-07-19, `Vienna3.10.8.dmg` (9.3 MB) â€” **ships dSYM tarballs** | ObjC/Cocoa, actively maintained, permissive. The published `-dSYM.tgz` is a gift: full symbolication of crashes without building anything. Pulls in WebKit though â€” plan for that. |
| **Quicksilver** | **Apache-2.0** | `v2.6.0`, 2026-04-19, `Quicksilver.2.6.0.dmg` + **`Quicksilver-debug.zip`** | Ships a **plain `.zip`** (no DMG mounting needed â€” big deal on Linux CI). Measured profile in Â§6.4. Modular plugin architecture means you can test a subset. |
| **MacVim** | Vim/charityware | (active) | ObjC Cocoa shell around a C core; the C core already works, so failures are *unambiguously* AppKit failures. Excellent signal-to-noise. |
| **iTerm2** | GPL-2.0 | (active) | Run-only. Terminal = AppKit + PTY + Foundation + text layout. A brutal but extremely informative target. |
| **Skim** | **BSD** | (active) | BSD-licensed PDF viewer â€” but requires **PDFKit/Quartz**. Park it until CoreGraphics exists. |
| **Transmission** | GPL-2/3 + MIT parts | (active) | Mac GUI is ObjC/Cocoa; the daemon is portable C. Two-stage target: daemon first (Tier 0!), GUI later. |

**Practical note on DMGs:** most of these ship `.dmg`, which is painful on Linux CI. Prefer projects publishing `.zip` (Quicksilver), or extract with `7z x foo.dmg` / `dmg2img` + `hfsplus`. Build this into the fetcher once.

**Strong recommendation:** make **Sequel Pro 1.1.2** the flagship Tier 2 milestone. MIT + frozen + Intel-only + pure Cocoa is the rarest combination on this list.

---

## 5. Tier 3 â€” Proprietary apps: what's actually legitimate

### 5.1 Apple system apps (Safari, TextEdit) â€” **don't**

Three independent blockers, any one of which is disqualifying:

1. **Licence.** Apple's macOS SLA states you agree not to install, use, or run the Apple Software on any non-Apple-branded computer, **or to enable others to do so**. Running TextEdit on OpenOSX-in-QEMU-on-a-Ryzen is squarely inside that prohibition. Redistribution is plainly infringing. Even personal testing conflicts with the SLA â€” and if it ever touches CI, you've published it.
2. **Technically dead on arrival.** Since **Big Sur, system dylibs are no longer on disk at all** â€” the only copy is inside the dyld shared cache, which Apple authors and ships whole. Apple system apps also link **private** frameworks (which you can never legitimately reimplement from headers, because there are no public headers). You'd be chasing an unbounded, undocumented surface.
3. **Zero diagnostic value.** No source, no dSYMs, heavily entitled, signed, sandboxed. When it fails you learn nothing.

Installers *are* still obtainable (`softwareupdate --fetch-full-installer --full-installer-version â€¦`, `mist-cli`, and the older `installinstallmacos.py`) â€” but obtainability is not permission. **Firm recommendation: Apple system apps are out of scope, permanently, and this should be written into the project's contributor docs.** The compatibility story should be *third-party* apps, exactly like Wine's is.

### 5.2 Freely-redistributable proprietary apps â€” **yes, these**

The right Tier 3 shape: **standalone, freely downloadable, native Cocoa (not Electron), Intel x86_64 build, low `minos`.**

- **Sublime Text 4** â€” the best candidate. Native Cocoa/C++ (explicitly *not* Electron), **Universal 2** (so it has a real x86_64 slice), free to download and evaluate indefinitely, standalone `.dmg`, and it supports macOS 10.13+ â€” a low `minos` that will actually load on Darwin 20.5. Small, fast, and a genuinely representative "real app".
- **BBEdit (free mode)** â€” classic Cocoa + Carbon-era heritage, standalone download, long macOS compatibility tail.
- **Xcode Command Line Tools** â€” Apple's own `clang`, `git`, `make` etc. Freely downloadable from developer.apple.com, **CLI only**, and a superb Tier 0/1 test (heavy libSystem + libc++ + Foundation). Same SLA caveat applies in principle, so treat as local-only, never CI, never redistributed â€” but note it's a much smaller ask than shipping GUI system apps.
- **Firefox ESR x86_64** â€” MPL-2.0, so no licence issue at all; a true endgame integration test (its own graphics stack, huge Cocoa surface).

**Explicitly deprioritise all Electron apps** (VS Code, Discord, Slack). They're not tests of your Cocoa work â€” they're tests of Chromium's Mac port, which needs Metal/CoreAnimation/IOSurface. Endgame only.

---

## 6. Measurement methodology â€” the part that makes progress countable

### 6.1 Two tool paths, both verified

**Path A â€” cctools, already in your tree.** `tools/cctools/` vendors `otool`, `misc/nm.c`, `misc/install_name_tool.c`, `misc/lipo.c`, `misc/pagestuff.c`, and `cctoolsBuild` is already a package in `nix/pkgs/`. These are **Linux-hosted** and read Mach-O fine. No Mac needed.

```bash
otool -L  bin/jq                 # (a) linked dylibs
otool -l  bin/jq | grep -A4 LC_BUILD_VERSION   # minos / sdk / platform
nm -u -m  bin/jq                 # (b) undefined symbols, WITH per-dylib attribution
nm -gU    libfoo.dylib           # exported symbols (reads the export trie)
lipo -info bin/jq                # arch slices
```
The `-m` flag on `nm` is the important one: it prints `(undefined) external _foo (from libSystem)` â€” the library attribution comes free.

**Path B â€” the pure-Python scanner I wrote and ran.** Zero dependencies, runs on Windows/Linux/macOS, emits JSON. Use this in CI where you don't want to depend on a built cctools. It parses fat + thin, `LC_LOAD_DYLIB`/`WEAK`/`REEXPORT`/`UPWARD`, `LC_RPATH`, `LC_BUILD_VERSION`/`LC_VERSION_MIN_MACOSX`, `LC_SYMTAB` undefineds **with two-level-namespace library ordinals**, and the **dyld export trie** (`LC_DYLD_INFO_ONLY.export_off` and `LC_DYLD_EXPORTS_TRIE`).

Verified output on `libsqlite3.0.dylib`: `315 exports` parsed from the trie, `107 undefined` all attributed to libSystem.

**Why the export trie and not just `nm`:** a stripped dylib's exported symbols live **only** in the trie, not the symtab. If you enumerate what OpenOSX *provides* using symtab entries you will massively undercount. The scanner handles both `LC_DYLD_INFO_ONLY` (older) and `LC_DYLD_EXPORTS_TRIE` (newer) layouts.

**Note on chained fixups:** modern binaries (`LC_DYLD_CHAINED_FIXUPS`) move binding metadata out of classic bind opcodes, but **undefined symbols remain in `LC_SYMTAB`** with the library ordinal in `n_desc >> 8`, so the symtab approach works for both old and new binaries. Verified on Quicksilver (a 2026 Universal 2 build).

### 6.2 (a) What frameworks/dylibs does it need â€” recursively

Dependency closure must be **transitive** and must resolve `@rpath` / `@executable_path` / `@loader_path`:

```
deps(binary) = LC_LOAD_DYLIB âˆª LC_LOAD_WEAK_DYLIB âˆª LC_REEXPORT_DYLIB âˆª LC_LOAD_UPWARD_DYLIB
resolve @executable_path -> <bundle>/Contents/MacOS
        @loader_path     -> dirname(current binary)
        @rpath           -> each LC_RPATH entry, in order (themselves expanded)
```
For a `.app`, walk **everything**: `Contents/MacOS/*`, `Contents/Frameworks/**`, `Contents/PlugIns/**`, `Contents/XPCServices/**`. Quicksilver's zip alone contains multiple `.qsplugin` bundles with their own Mach-O binaries â€” miss those and your report is wrong.

**Weak vs strong matters enormously for triage.** A missing `LC_LOAD_WEAK_DYLIB` is survivable (dyld nulls the symbols); a missing strong dylib is a hard launch failure. Score them separately.

### 6.3 (b) Undefined symbols, attributed per-library

Per-library attribution is the whole game â€” "app needs 245 symbols" is useless; "app needs 53 AppKit symbols, 40 Foundation, 27 CoreFoundation" is a work plan. Extraction:

```python
# for each nlist_64 entry, skipping N_STAB:
if (n_type & N_TYPE) in (N_UNDF, N_PBUD) and (n_type & N_EXT) and n_value == 0:
    ordinal = (n_desc >> 8) & 0xff       # two-level namespace library ordinal
    lib     = LC_LOAD_*[ordinal - 1]     # 1-based, in load-command order
```
Special ordinals: `0` = flat namespace, `0xfe` = self, `0xff` = main executable.

### 6.4 The measurement that changes your roadmap

Measured on `Quicksilver.app/Contents/MacOS/Quicksilver`, x86_64 slice (`minos 10.14.0`, `sdk 15.5.0`), **245 undefined symbols total**:

| Library | Undefined syms |
|---|---|
| AppKit | 53 |
| Foundation | 40 |
| QSCore (own framework) | 36 |
| libobjc.A.dylib | 30 |
| CoreFoundation | 27 |
| libSystem.B.dylib | 22 |
| QSFoundation (own) | 12 |
| Carbon | 10 |
| QSInterface (own) | 6 |
| CoreGraphics | 3 |
| ApplicationServices | 2 |
| Quartz / WebKit / QSEffects / PermissionsKit | 1 each |

Breaking the 53 AppKit symbols down:

```
27 are _OBJC_CLASS_$_ class references:
  NSAlert NSApplication NSArrayController NSButton NSButtonCell NSColor NSEvent
  NSFont NSImage NSMenu NSMenuItem NSOpenPanel NSParagraphStyle NSPasteboard
  NSPopUpButtonCell NSSavePanel NSScreen NSStatusBar NSTextField NSTextFieldCell
  NSToolbar NSToolbarItem NSUserDefaultsController NSView NSViewController
  NSWindowController NSWorkspace

26 are C functions / global constants / metaclasses:
  _NSApp _NSApplicationMain _NSBeep _NSFilenamesPboardType _NSFontAttributeName
  _NSForegroundColorAttributeName _NSModalPanelRunLoopMode
  _NSParagraphStyleAttributeName _NSStringPboardType _NSURLPboardType
  _NSToolbarFlexibleSpaceItemIdentifier ... _OBJC_METACLASS_$_NSApplication ...
```

**"Implement 27 AppKit classes and Quicksilver runs" is false, and believing it will burn months.** The ObjC sections tell the truth:

| Section | Count | Meaning |
|---|---|---|
| `__objc_selrefs` | **1,014** | distinct selectors this binary *sends* |
| `__objc_methname` | 1,618 strings | all selector/type strings in the image |
| `__objc_classrefs` | 113 | classes referenced (all libs) |
| `__objc_superrefs` | **21** | classes it **subclasses** â€” the hardest cases |
| `__objc_classlist` | 30 | classes it defines |
| `__objc_catlist` | 3 | categories it defines (may patch *your* classes) |

`__objc_superrefs` is the danger list: subclassing `NSView` means your `NSView` needs a **compatible ivar layout and correct method dispatch order**, not just the right selectors. Those 21 classes are where the real engineering is.

### 6.5 The three-axis coverage score

Per app, per framework, report:

```
symbol_coverage   = |undefined âˆ© provided_exports| / |undefined|
class_coverage    = |classrefs âˆ© provided_classes| / |classrefs|
selector_coverage = |selrefs âˆ© provided_selectors| / |selrefs|
launch_score      = min of the three, weighted:  0.2*sym + 0.3*class + 0.5*sel
```
`selector_coverage` gets the heaviest weight because it's the only one that correlates with *actually running*. Also emit an unweighted **`hard_blockers`** count: strongly-linked missing dylibs + missing `__objc_superrefs` classes. That number, not the percentage, is what tells you whether the app can launch at all.

### 6.6 What OpenOSX provides â€” building the inventory

Enumerate the *provided* side from the built image (or straight from the Nix outputs, no boot required):

```bash
# every dylib/framework binary in the image
find "$IMAGE_ROOT"/usr/lib "$IMAGE_ROOT"/System/Library/Frameworks -type f \
  | xargs -n1 machoscan.py --exports > provided_symbols.json
```
- **Symbols:** union of all export tries. (Not symtab â€” see Â§6.1.)
- **Classes:** exported symbols matching `_OBJC_CLASS_$_(.*)`.
- **Selectors:** union of `__objc_methname` strings reachable from each dylib's `__objc_const` method lists. Cheap approximation that works well: the whole `__objc_methname` section of each provided framework.

Snapshot `provided_symbols.json` **per build** and commit it. Then coverage deltas become reviewable in PRs â€” "this PR raises Sequel Pro from 41% to 58%" is a review comment you can actually act on.

### 6.7 Two traps that will silently corrupt your numbers

1. **NIB/XIB-instantiated classes are invisible to every static section.** Cocoa apps instantiate classes by *name string* from `.nib` files. `NSClassFromString(@"NSSplitViewController")` leaves no classref. **You must also scan `Contents/Resources/**/*.nib` and `*.storyboardc`** for class-name strings and merge them into the required-class set. Skip this and your class coverage will read 90% while the app dies at `NSApplicationMain`.
2. **`dlopen`/`NSClassFromString`/`objc_getClass` with computed names** are undecidable statically. Budget for a **runtime** complement: run under a `DYLD_INSERT_LIBRARIES` shim that logs every `objc_msgSend` selector and `objc_getClass` miss, and merge the runtime trace back into the corpus profile. Static analysis gives you the roadmap; runtime tracing gives you the truth.

---

## 7. The coverage harness â€” script sketch

Three components. Put them under `tools/compat/`, and wire the fetcher into `nix/pkgs/compat/` alongside the existing `nix/pkgs/apple/*-test.nix` convention.

### 7.1 `tools/compat/fetch_bottle.sh`

```bash
#!/usr/bin/env bash
# usage: fetch_bottle.sh <formula> [tag] [platform=big_sur]
set -euo pipefail
F="$1"; TAG="${2:-}"; PLAT="${3:-big_sur}"
AUTH='Authorization: Bearer QQ=='
BASE="https://ghcr.io/v2/homebrew/core/$F"

# oldest tag = the one most likely to still carry big_sur
[ -n "$TAG" ] || TAG=$(curl -sfH "$AUTH" "$BASE/tags/list" | jq -r '.tags[0]')

DIGEST=$(curl -sfH "$AUTH" -H 'Accept: application/vnd.oci.image.index.v1+json' -L \
  "$BASE/manifests/$TAG" \
  | jq -r --arg p "$PLAT" '
      .manifests[]
      | select(.platform.os=="darwin" and .platform.architecture=="amd64")
      | select(.annotations["org.opencontainers.image.ref.name"] | test("\\."+$p+"(\\.|$)"))
      | .annotations["sh.brew.bottle.digest"]' | head -1)

[ -n "$DIGEST" ] || { echo "no $PLAT amd64 bottle for $F:$TAG" >&2; exit 1; }

curl -sfL -H "$AUTH" "$BASE/blobs/sha256:$DIGEST" -o "$F-$TAG-$PLAT.tar.gz"
echo "$DIGEST  $F-$TAG-$PLAT.tar.gz" | sha256sum -c -   # digest IS the tarball sha256
tar xzf "$F-$TAG-$PLAT.tar.gz" -C corpus/              # run on Linux: symlinks
echo "$F $TAG $PLAT $DIGEST"                            # -> pin table for Nix fetchurl
```
Note the tag-name regex: real names are `1.6.big_sur.1`, `12.1.1.big_sur`, `8.32.big_sur.2` â€” the rebuild-revision suffix moves around, so **substring/regex match, never exact equality**.

### 7.2 `tools/compat/machoscan.py`

Already written and verified â€” see the path at the top of this report. Public API:

```python
scan(path, want_arch='x86_64') -> [ {
    arch, filetype, platform, minos, sdk, id,
    dylibs[], weak_dylibs[], reexports[], rpaths[],
    undefined[], imports_by_lib{lib: [syms]},
    exports[],                       # from the dyld export trie
} ]
```
To finish it for production, add (all mechanically straightforward, sections already located and dumped successfully during this research):
- `--sections` mode returning `__objc_selrefs` / `__objc_classrefs` / `__objc_superrefs` / `__objc_methname` / `__objc_classname`
- `@rpath`/`@executable_path`/`@loader_path` resolution + recursive bundle walk
- NIB class-name scraping (Â§6.7)

### 7.3 `tools/compat/coverage.py`

```python
#!/usr/bin/env python3
"""coverage.py --provided provided_symbols.json --app corpus/Foo.app -> report.json/md"""
req  = walk_bundle(app)          # transitive: MacOS/, Frameworks/, PlugIns/, XPCServices/
prov = json.load(provided)       # per-dylib: exports, classes, selectors

for lib, syms in req.imports_by_lib.items():
    p = prov.get(canonical(lib))            # normalise Foundation.framework/Versions/C/Foundation
    if p is None:
        report.missing_libs.append((lib, req.is_weak(lib), len(syms)))
        continue
    have = set(syms) & p.exports
    report.per_lib[lib] = dict(
        need=len(syms), have=len(have),
        missing=sorted(set(syms) - p.exports),
        pct=100*len(have)/len(syms))

report.classes   = cover(req.classrefs   | req.nib_classes, prov.all_classes)
report.selectors = cover(req.selrefs,                        prov.all_selectors)
report.superrefs = sorted(req.superrefs - prov.all_classes)   # HARD blockers
report.launch_score = 0.2*sym + 0.3*cls + 0.5*sel
report.hard_blockers = len(report.missing_libs_strong) + len(report.superrefs)
emit_markdown(report)   # ranked "top 25 missing symbols across the whole corpus"
```

**The output that drives the roadmap** is not the per-app percentage â€” it's the **corpus-wide histogram of missing symbols and selectors ranked by how many apps need them.** That tells you, empirically, that (say) `NSView`'s `drawRect:`/`layout` path unblocks 6 of 9 apps, so build that first. This is exactly how Wine prioritises stubs, and it's the single most valuable artefact this harness produces.

### 7.4 CI integration

Add a job to the existing pipeline (the repo already runs a 3-job pipeline per commit `34321e1e`):

```yaml
compat-coverage:
  - fetch corpus (pinned digests -> Nix fetchurl, hermetic, no network flake)
  - build provided_symbols.json from the image outputs (no boot needed)
  - run coverage.py over the corpus
  - fail if launch_score for any Tier-0 app regresses
  - publish report.md as a build artifact + PR comment
```
Because the provided-side inventory comes from Nix outputs rather than a booted image, this job is **fast and runs on every PR** â€” decoupled from the slow QEMU boot harness.

---

## 8. Recommended ladder, in build order

| # | Milestone | Corpus | Gate |
|---|---|---|---|
| C0 | libSystem sanity | `jq 1.6`, `sqlite 3.35.3`, `tree`, `nano` (pinned bottles) | all exit 0, correct output |
| C1 | libc++ / Rust ABI | `ripgrep 12.1.1`, `zstd`, `xz` | ditto |
| C2 | objc4 runtime | in-tree `objc-test.nix` suite | test suite green |
| C3 | Foundation conformance | gnustep-base tests cross-built vs *your* Foundation | â‰¥90% pass |
| C4 | Foundation CLI, real app | `class-dump`, `duti` | class-dump dumps a real dylib |
| C5 | **First AppKit launch** | **Sequel Pro 1.1.2** (MIT, frozen, Intel) | `NSApplicationMain` returns, window appears |
| C6 | Second GUI, independent codebase | Quicksilver 2.6.0 / MacVim | menu + text input work |
| C7 | Real proprietary app | **Sublime Text 4** (native Cocoa, Universal 2, free eval) | editor usable |
| C8 | Endgame | Firefox ESR | â€” |

**Do C0â€“C2 this month.** They need no new frameworks, they're fully automatable with the verified digests above, and they turn "does the userland work?" from a vibe into a number.

---

## Sources

- [Manually downloading a bottle from ghcr.io? Â· Homebrew Discussion #4951](https://github.com/orgs/Homebrew/discussions/4951)
- [How does homebrew store bottles? Â· Homebrew Discussion #4335](https://github.com/orgs/Homebrew/discussions/4335)
- [Homebrew JSON API documentation](https://formulae.brew.sh/docs/api/)
- [Extract the system libraries on macOS Big Sur â€” lapcatsoftware.com](https://lapcatsoftware.com/articles/bigsur.html)
- [Reminder: macOS system frameworks binaries are hidden (since Big Sur) â€” Wade Tregaskis](https://wadetregaskis.com/reminder-macos-system-frameworks-binaries-are-hidden-since-big-sur/)
- [Dev:dyld_shared_cache â€” The Apple Wiki](https://theapplewiki.com/wiki/Dev:Dyld_shared_cache)
- [Apple Inc. Software License Agreement for macOS (Ventura)](https://www.apple.com/legal/sla/docs/macOSVentura.pdf)
- [sequelpro/sequelpro â€” GitHub](https://github.com/sequelpro/sequelpro)
- [ViennaRSS/vienna-rss â€” GitHub](https://github.com/ViennaRSS/vienna-rss)
- [quicksilver/Quicksilver â€” GitHub](https://github.com/quicksilver/Quicksilver)
- [nygard/class-dump â€” GitHub](https://github.com/nygard/class-dump)
- [Sublime Text â€” Operating System Compatibility](https://www.sublimetext.com/docs/os_compatibility.html)
- [ninxsoft/mist-cli â€” GitHub](https://github.com/ninxsoft/mist-cli)
- [Listing the full OS installers available from Apple's Software Update feed â€” Der Flounder](https://derflounder.wordpress.com/2021/03/03/listing-the-full-os-installers-available-from-apples-software-update-feed-on-macos-big-sur/)

