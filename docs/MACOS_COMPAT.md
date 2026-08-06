# Running macOS Applications on OpenOSX

**Status:** design doc. Authoritative for the compatibility workstream (phases C0â€“C4).
**Audience:** implementers. Assumes familiarity with Mach-O, dyld, and the Objective-C runtime.

---

## 1. Executive summary

OpenOSX is not an emulator and not a translation layer: it runs the genuine XNU 20.5 kernel with an Apple-lineage userland built from Apple's own published sources, so a Mach-O x86_64 binary is a *native* binary here â€” the same dyld, the same objc4, the same CoreFoundation 1338, the same libSystem, XPC and launchd that Apple ships. That is a starting position no other project has: Wine had to reimplement the loader, the C runtime and the object model before it could run anything, and Darling has to interpose a Linux kernel; we start with all of that already correct and already ours. What is missing is a single well-bounded band of the stack â€” the frameworks Apple has never published: AppKit, CoreGraphics/Quartz, CoreAnimation, WindowServer/CGS, Metal, AVFoundation, CoreAudio, and the Darwin Swift overlays. Today's graphics stack (Mesa, X11/Wayland, GTK3, PDGOP/PDSurface, XFCE) is a Linux-shaped stack cross-built for Darwin; it gets pixels on screen but presents none of the API surface a macOS binary looks for. The purpose of this document is to make the remaining distance *countable* rather than vibes-based: every macOS binary declares exactly what it needs, and we can measure how much of that we supply â€” per app, per framework, on every commit. Everything below assumes the clean-room rules in Â§3 are non-negotiable; a single tainted contributor costs more than any amount of engineering time saved.

---

## 2. The gating mechanism: dyld symbol resolution

A macOS binary is not a black box. Every Mach-O carries a complete, machine-readable declaration of its requirements, and dyld enforces that declaration at launch:

1. **`LC_BUILD_VERSION` / `LC_VERSION_MIN_MACOSX`** â€” platform and `minos`. dyld refuses to load a binary whose `minos` exceeds the running system. Darwin 20.5 reports macOS 11.4, so `minos 11.0` (Big Sur) binaries load and `minos 12.0+` binaries do not. This gate precedes every other consideration.
2. **`LC_LOAD_DYLIB` / `_WEAK_` / `_REEXPORT_` / `_UPWARD_`** â€” the dylib closure. A missing strong dylib is a hard launch failure; a missing weak dylib is survivable (dyld nulls the symbols and the app is expected to check). Score them separately.
3. **`LC_SYMTAB` undefined externals**, each tagged with a **two-level-namespace library ordinal** in `n_desc >> 8`. This is the exact list of symbols the binary demands from each dylib. One missing symbol and dyld aborts with `Symbol not found`.

So compatibility progress is measurable: *for each app, what fraction of its declared imports do we export?* No guessing, no "it feels 60% done." A missing symbol is a work item with a name.

**The honest caveat, and it matters enormously.** Symbol coverage alone will lie to you for GUI apps, because **Objective-C method calls are not symbols**. Measured on the x86_64 slice of Quicksilver 2.6.0: 245 undefined symbols total, of which only **53 are AppKit** â€” but the same binary carries **1,014 `__objc_selrefs`** (selectors it actually sends) and **113 `__objc_classrefs`**. Implementing 27 AppKit classes does not make Quicksilver run; it makes it link, then crash on the first unimplemented selector.

The metric is therefore **three axes**, not one:

| Axis | Source | Answers |
|---|---|---|
| `symbol_coverage` | `LC_SYMTAB` undefined âˆ© our export tries | will it *link* |
| `class_coverage` | `__objc_classrefs` + NIB class-name strings âˆ© our `_OBJC_CLASS_$_*` | will it *instantiate* |
| `selector_coverage` | `__objc_selrefs` âˆ© our `__objc_methname` | will it *run* |

```
launch_score  = 0.2*symbol + 0.3*class + 0.5*selector
hard_blockers = |missing strong dylibs| + |missing __objc_superrefs classes|
```

`hard_blockers`, not the percentage, is what says whether an app can launch at all. `__objc_superrefs` â€” the classes a binary *subclasses* â€” is the danger list: subclassing `NSView` requires a compatible ivar layout and correct dispatch ordering, not merely the right selectors. Quicksilver has 21 of them. Those 21 are where the real engineering is.

Two static-analysis blind spots to budget for:

- **NIB/XIB-instantiated classes are invisible to every Mach-O section.** Cocoa instantiates by name string from `.nib` / `.storyboardc`. We must scrape those files for class names and merge them into the required-class set, or class coverage reads 90% while the app dies inside `NSApplicationMain`.
- **`dlopen` / `NSClassFromString` / `objc_getClass` with computed names are statically undecidable.** The complement is a runtime trace: a `DYLD_INSERT_LIBRARIES` shim logging every `objc_msgSend` selector and `objc_getClass` miss, merged back into the app's profile. Static analysis gives the roadmap; runtime tracing gives the truth.

---

## 3. Clean-room rules (read before writing a line)

OpenOSX is in a far better position than ReactOS ever was, because most of what we ship is Apple's *published* APSL/BSD source, not reverse-engineered material. Clean-room discipline applies to a bounded perimeter and nowhere else.

**The perimeter.** XNU, libSystem, dyld, objc4, CoreFoundation, libc++, Security, IOKit, launchd, mDNSResponder, and the published parts of Foundation/CoreServices/XPC are *licensed use*, not reverse engineering â€” no special handling. The clean-room regime covers exactly: **AppKit, CoreGraphics/Quartz, CoreAnimation, WindowServer/CGS, Metal, AVFoundation, CoreAudio, and the Darwin Swift overlays.** That subtree, and only that subtree, is governed by the rules below.

### Tier A â€” GREEN, always allowed

- Apple's published open source (opensource.apple.com, github.com/apple, swiftlang).
- Apple's public developer documentation **as a source of facts, not of text**. Read it, then write your own declarations and your own prose. Never paste.
- Public SDK headers and `.swiftinterface` files â€” **build-time only** (see below).
- Published third-party RE write-ups, conference talks, books.
- Open-source reimplementations under compatible licenses.
- **Observed behaviour** of Apple binaries on lawfully-owned hardware: `nm` / `otool` / `class-dump` symbol and selector enumeration, `lldb`, `dtrace`, Mach message traces, `Info.plist` contents, struct sizes probed with `sizeof`, error codes and their triggers.

### Tier B â€” AMBER, two-role split required

Static disassembly or decompiler output (Ghidra/IDA/Hopper) of Apple binaries. Legally defensible (Sony v. Connectix; EU Software Directive Art. 6) but procedurally where ReactOS died. **The person who disassembles must not be the person who implements that function.** The disassembler writes prose into `docs/spec/<Framework>/<symbol>.md` â€” parameters, semantics, ordering, error conditions, observable side effects â€” with **no pseudocode, no variable names, no control-flow transcription, no register detail**. The implementer works from the spec only. CI enforces `Spec-Author â‰  commit author`. **On a one-person effort Tier B is closed; stay in Tier A.** Say that out loud rather than pretending otherwise.

### Tier C â€” RED, contribution-disqualifying

Any non-public Apple source, however obtained: the iOS/macOS leaks, internal headers, internal SDK trees, GitHub mirrors of leaked trees, screenshots or excerpts posted anywhere. Also material under NDA, or knowledge acquired as an Apple employee or contractor under confidentiality.

**A contributor who has read any of the above must not contribute to the clean-room subtree at all.** They remain welcome everywhere else â€” packaging, CI, XNU, userland, desktop, docs, testing. ReactOS declined to draw this line and paid for it for fifteen years. We draw it in advance, in writing, before the first commit.

Note what taints and what does not: **only Apple's non-public source taints.** Reading a GPL app's source to debug why *it* crashes on our AppKit teaches you nothing about Apple's implementation â€” it is the caller, not the callee. Test-binary licensing is a distribution question, not a taint question (Â§4).

### The interface/expression line

**Interface facts â€” allowed, record the source:** exported symbol names; ObjC class, selector and protocol names; method signatures and type encodings; ivar names and offsets of public classes; struct layouts and field offsets; enum and constant values; notification names; defaults and plist keys; Mach message IDs; mig subsystem numbers; framework install names; the sequence of selectors an app sends.

**Expression â€” forbidden regardless of how learned:** Apple's algorithms and their internal structure; **unexported/private function names, private struct member names, macro naming conventions** (this trio is precisely the fingerprint that convicted ReactOS in 2019); comments; non-ABI string literals; magic constants whose derivation you cannot independently explain; code layout and helper decomposition.

**Test to hand a contributor:** *if a competent engineer with only public docs and observed behaviour would have to write it this way for it to work, it is an interface fact. If they could reasonably have written it differently and yours matches Apple's, explain why.*

### Per-file provenance (mandatory in the clean-room subtree)

```
// SPDX-License-Identifier: APSL-2.0
// OpenOSX-Provenance: tier-a
// OpenOSX-Sources:
//   - https://developer.apple.com/documentation/appkit/nsview  (fetched 2026-08-07)
//   - observed: -[NSView drawRect:] call ordering, macOS 11.4, lldb trace
//   - Cocotron NSView.m (MIT) â€” adapted, see THIRD_PARTY_LICENSES.md
// OpenOSX-Author-Attestation: no non-public Apple source consulted
```

Tier B files add `OpenOSX-Spec:` and `OpenOSX-Spec-Author:`. CI greps for the header on every file under the clean-room subtree and fails the build if it is missing.

### Supporting practice

- `CONTRIBUTING.md` carries a DCO extended with: *"I certify that I have not consulted any non-public Apple source code, internal documentation, or material obtained under confidentiality in producing this contribution, and that its provenance header is accurate."* `Signed-off-by:` on every commit. Highest value per unit of effort of anything in this document.
- **Never squash the clean-room history.** A trail showing wrong guesses and iteration is affirmative evidence of independent creation; magic constants correct on the first try are not.
- **Ship zero Apple assets** â€” no icons, cursors, wallpapers, sounds, asset catalogs, or system fonts. CI check on the image, not a rule people remember.
- **Never distribute Apple binaries.** If a test needs a real Apple framework, the *user* supplies it from their own licensed Mac; we ship nothing.
- SDK headers and `.swiftinterface` files: use via the existing `requireFile` pattern (`nix/pkgs/apple/foundation.nix`), never vendored into the repo, never in CI cache. Prefer APSL headers where they exist; otherwise re-declare from public documentation. Whether a dylib compiled *from* a `.swiftinterface` is shippable is unresolved â€” answer it before writing overlay code, not after.

### License architecture for shippable image components

APSL-2.0 is GPL-incompatible. Same-image aggregation is fine (we already ship GPL XFCE beside APSL components); **combination** â€” static linking, vendoring, deriving â€” is not.

| Source | License | Vendor into a framework? | Note |
|---|---|---|---|
| Cocotron | MIT | **Yes â€” the intended AppKit base** | itself clean-room; inherits a clean chain |
| Skia | BSD-3 | **Yes â€” the intended CGContext backend** | `SkCanvas` â‰ˆ `CGContext`; better fit than Cairo |
| Swift runtime, swift-corelibs | Apache-2.0 + RLE | Yes | RLE removes the attribution burden |
| Cairo | LGPL-2.1 / MPL-1.1 | Only under the MPL-1.1 option | file-level copyleft |
| GNUstep | LGPL-2.1+ | **No** â€” dynamic link at most, never core | do not let AppKit *be* GNUstep |
| WebKit | LGPL-2.1 (+ BSD parts) | No â€” dynamic link only | |
| Darling | GPL-3.0 | **No â€” study only** | `darling-cocotron` MIT files OK, verify per file |

Every vendored component gets an entry in `THIRD_PARTY_LICENSES.md` with upstream commit hash and per-file license notes.

---

## 4. Tiered capability ladder

Test binaries are fetched on demand into a cache â€” never committed, never shipped in the image. License is therefore near-irrelevant for *test targets*; GPL apps are fine to run. Permissive licensing is preferred only because it lets us fork and instrument.

**Tier 0 â€” pure libc.** Homebrew still serves `big_sur` **x86_64** bottles from ghcr.io for older formula versions; they are `minos 11.0.0`, an exact match for Darwin 20.5. Verified targets, pinned by content-addressed digest: `jq 1.6`, `sqlite 3.35.3`, `ripgrep 12.1.1`, `coreutils 8.32`, `tree 1.8.0`, `zstd 1.4.9`, `xz 5.2.5`, `nano 5.6.1`, `oniguruma 6.9.6`, `lua 5.4.3`, `pcre2 10.36`. `jq 1.6` imports **137 symbols from exactly two dylibs** (libSystem + libonig). Needs: libSystem, dyld, nothing else. These are the regression canaries â€” if they fail, the bug is in the base userland, not in framework coverage. *Gotchas:* bottle paths contain a literal `@@HOMEBREW_PREFIX@@` that must be rewritten with `install_name_tool`; extract on Linux (bottles contain symlinks); never substitute a `monterey`/`sonoma` tag for a missing `big_sur` one â€” `minos` is enforced.

**Tier 1 â€” Objective-C / Foundation, no graphics.** Needs objc4, CoreFoundation, Foundation. In build order: the in-tree **objc4 test suite** (`nix/pkgs/apple/objc-test.nix` â€” APSL, already legal and in-tree, hundreds of programs exercising `objc_msgSend`, categories, associated objects, ARC, weak refs, `+load`, protocols); the **libc++ and libdispatch suites**; **gnustep-base's test suite cross-built against *our* Foundation** (LGPL â€” runnable, not vendorable â€” thousands of `NSString`/`NSArray`/`NSDictionary`/`NSFileManager`/`NSURL`/`NSRunLoop` assertions, a free differential conformance suite); **`class-dump`** (GPL, run-only), pure Foundation and doubling as an in-guest diagnostic once it works; **`duti`** (stresses LaunchServices/CoreServices); **`trash`** and **`terminal-notifier`** (MIT) â€” the latter is really Tier 1.5 and smokes out how far an `NSApplication` run loop gets with no window server.

**Tier 2 â€” open-source Cocoa GUI apps.** Needs CoreGraphics, AppKit, a CGS/WindowServer shim.

- **Sequel Pro 1.1.2** (MIT, 2016, pure Intel x86_64, frozen) â€” **the flagship first GUI target.** MIT + frozen + Intel-only + pure Cocoa is the rarest combination available; NSTableView/NSSplitView/NSToolbar is a broad but classic surface.
- **Quicksilver 2.6.0** (Apache-2.0) â€” ships a plain `.zip` (no DMG mounting on CI) plus a debug build; profiled in Â§2.
- **MacVim** (charityware) â€” an ObjC shell over a working C core, so every failure is unambiguously an AppKit failure. Best signal-to-noise on the list.
- **Vienna RSS** (Apache-2.0) â€” publishes dSYM tarballs, so crashes symbolicate for free; pulls in WebKit.
- **iTerm2** (GPL, run-only); **Transmission** (daemon is Tier 0, GUI is Tier 2); **Skim** (BSD, parked until PDFKit/Quartz exists).

**Tier 3 â€” freely-redistributable proprietary Cocoa apps.** **Sublime Text 4** is the target: native Cocoa/C++ (explicitly not Electron), Universal 2 so it has a real x86_64 slice, macOS 10.13+ so `minos` is low, standalone download, free indefinite evaluation. **BBEdit (free mode)** as a second. Electron apps (VS Code, Slack, Discord) are deprioritised â€” they test Chromium's Mac port and need Metal/CoreAnimation/IOSurface, not our Cocoa work. **Firefox ESR** (MPL-2.0) is the endgame integration test.

**Out of scope, permanently: Apple's own applications.** Safari, TextEdit and friends are disqualified three times over. The macOS SLA prohibits running Apple software on non-Apple hardware or enabling others to do so. Since Big Sur the system dylibs they need exist only inside the dyld shared cache, which Apple authors whole. And they link *private* frameworks that have no public headers and can never be legitimately reimplemented. They also have zero diagnostic value: no source, no symbols, signed and sandboxed. Our compatibility story is third-party apps, exactly like Wine's. Write this into `CONTRIBUTING.md`.

---

## 5. The Swift gap

Swift splits into four tiers with very different economics:

| Tier | Examples | Open source? | Action |
|---|---|---|---|
| Core runtime | `libswiftCore`, `SwiftOnoneSupport`, `RemoteMirror` | **Yes** (Apache-2.0 + RLE) | **Build it.** Tractable now |
| Platform overlays | `libswiftDarwin`, `ObjectiveC`, `Dispatch`, `os`, `simd`, `XPC` | **No** â€” removed from the repo | Reimplement; thin |
| Framework overlays | `libswiftFoundation`, `libswiftAppKit`, `libswiftCoreGraphics` | **No** | Gated on the ObjC framework existing |
| Compatibility shims | `libswiftCompatibility50.a` etc. | Yes | **Nothing to do** â€” already static-linked into the app |

`stdlib/public/Darwin` **does not exist** in `swiftlang/swift` on `release/5.5`; Apple deliberately removed the SDK overlays. That single fact defines the area: the core runtime is a solved problem and the useful surface is not.

**Roadmap placement: do the core runtime early and in parallel; defer everything else.** It is independent of graphics â€” it touches objc4, libSystem, dyld and the cross toolchain, all of which we already own. The Swift runtime is also a uniquely demanding client of objc4 (metadata initializer callbacks, `_objc_realizeClassFromSwift`, `objc_setHook_getClass`, non-pointer isa, tagged pointers, image-notification hooks) and will find gaps in our objc4 far more cheaply now than during AppKit bring-up. And "OpenOSX runs an unmodified Apple-built Swift binary" is a striking milestone for the cost.

**`libswiftFoundation` is a Foundation project wearing a Swift hat**, and this is the uncomfortable finding. It requires a real Objective-C Foundation; ours is currently a deliberate six-file slice (`NSString`, `NSCFString`, `NSArray`, `NSDictionary`, `NSURL`, `NSError`). `swift-corelibs-foundation` is not the escape hatch: it targets non-Darwin platforms by design, it defines Swift-native `NSString`/`NSArray` that collide with our toll-free-bridged ObjC ones, and Apple has confirmed on the record (to PureDarwin, on the Swift forums) that standalone CoreFoundation builds are no longer supported and CF is being progressively reimplemented to delegate *upward* into Swift. **A full ObjC Foundation port is a mandatory, multi-engineer-year line item that currently appears nowhere in our roadmap.** Surfacing that is more valuable than anything Swift-specific.

Practical notes for whoever picks this up. **Build an unmodified upstream stdlib â€” patch the build system, never stdlib internals** â€” because `@inlinable` code is already compiled into Apple-built clients and hardcodes `@frozen` layouts; a one-byte disagreement is silent memory corruption, not a link error. Build the *newest* stdlib that works rather than the "matching" 5.4: the ABI is forward-designed and newer is a superset, but newer also assumes newer objc4 features. Settle it with a spike â€” build 5.4, 5.8 and 5.10 for `x86_64-apple-macos11.0` and diff each one's undefined symbols against our objc4 + libSystem exports. That experiment sets the ceiling for the whole workstream and costs days. Run it on the Mac SSH build node; Linux-host â†’ macOS-target stdlib builds are explicitly unsupported upstream. Also note `libswift_Concurrency` is not in macOS 11 â€” apps targeting 11 back-deploy their own copy, so we likely need not ship one initially.

**SwiftUI is not on this roadmap at any horizon worth a number.** Assume AppKit apps are the target and SwiftUI apps are not.

---

## 6. Measurement tooling â€” `tools/compat/`

Three pieces, all running on Linux with no Mac and no booted image, so this is a fast CI job decoupled from the slow QEMU boot harness.

```
tools/compat/
  fetch_corpus.sh   # ghcr.io bottles + GitHub releases -> corpus/, digests pinned
  machoscan.py      # Mach-O reader: zero deps, emits JSON
  coverage.sh       # driver: provided inventory x corpus -> report.json + report.md
```

**Why not just cctools.** `tools/cctools/` already vendors Linux-hosted `otool`, `nm`, `install_name_tool`, `lipo` and `pagestuff`, and `cctoolsBuild` is already a package in `nix/pkgs/`; `nm -u -m` even gives per-dylib attribution for free. Use it for interactive work. For CI, `machoscan.py` avoids depending on a built toolchain and emits structured JSON.

**Critical detail: read the dyld export trie, not the symtab, for the *provided* side.** A stripped dylib's exports live only in `LC_DYLD_INFO_ONLY.export_off` / `LC_DYLD_EXPORTS_TRIE`. Enumerating what OpenOSX provides from symtab entries massively undercounts. Conversely, for the *required* side the symtab is correct even for modern binaries using `LC_DYLD_CHAINED_FIXUPS`, because undefined externals stay in `LC_SYMTAB` with the library ordinal in `n_desc >> 8`.

**`coverage.sh` inputs.**

1. `--provided provided_symbols.json` â€” the inventory of what the image exports, built from **Nix outputs, not a booted image**: walk `usr/lib` and `System/Library/Frameworks` and for each dylib record its export-trie symbols, the subset matching `_OBJC_CLASS_$_(.*)`, and its `__objc_methname` strings (a cheap, adequate approximation of provided selectors).
2. `--app corpus/Foo.app`, or a bare executable. For a bundle, walk **everything**: `Contents/MacOS/*`, `Contents/Frameworks/**`, `Contents/PlugIns/**`, `Contents/XPCServices/**`. Quicksilver's zip alone carries several `.qsplugin` bundles with their own Mach-O binaries; miss them and the report is wrong.
3. Dependency closure is transitive and resolves `@executable_path` â†’ `<bundle>/Contents/MacOS`, `@loader_path` â†’ dirname of the current binary, `@rpath` â†’ each `LC_RPATH` in order (themselves expanded).

**`coverage.sh` outputs.**

- Per app: `minos`, arch slices, strong/weak missing dylibs, per-library `need / have / missing[] / pct`, class coverage (classrefs **plus NIB-scraped class-name strings**), selector coverage, the `__objc_superrefs` set we do not provide, `launch_score`, and `hard_blockers`.
- **Corpus-wide: a histogram of missing symbols and selectors ranked by how many apps need them.** This â€” not any per-app percentage â€” is the artifact that drives the roadmap. It tells you empirically that, say, the `NSView` `drawRect:`/`layout` path unblocks six of nine apps, so build that first. It is exactly how Wine prioritises stubs and it is the single most valuable thing this harness produces.
- `provided_symbols.json` is snapshotted per build and **committed**, so coverage deltas become reviewable: *"this PR raises Sequel Pro from 41% to 58%"* is a review comment you can act on.

**CI job `compat-coverage`**, alongside the existing three: fetch the pinned corpus via `fetchurl` (content-addressed, hermetic, never flakes), build the provided inventory from image outputs, run `coverage.sh` over the corpus, **fail on any Tier-0 regression**, publish `report.md` as an artifact and a PR comment.

---

## 7. Phase plan

### C0 â€” Measurement and base-userland canaries

**Deliverables:** `tools/compat/{fetch_corpus.sh,machoscan.py,coverage.sh}`; pinned Tier-0 bottle digests in `nix/pkgs/compat/`; `provided_symbols.json` generated from image outputs; the `compat-coverage` CI job; `docs/CLEANROOM_POLICY.md`, the DCO clause in `CONTRIBUTING.md`, `THIRD_PARTY_LICENSES.md` and an empty `docs/spec/` tree â€” **all four land before the first clean-room line of code, because a policy's date is its evidence.**
**Success:** `jq 1.6`, `sqlite3 3.35.3`, `tree`, `nano` run in the VM and exit 0 with correct output; `ripgrep 12.1.1`, `zstd`, `xz` likewise (libc++ / Rust ABI); the coverage report publishes on every PR. *If these fail the bug is in libSystem or dyld, not in AppKit.*

### C1 â€” Runtime and Foundation conformance (Swift core runtime in parallel)

**Deliverables:** in-tree objc4, libc++ and libdispatch test suites green in the VM; gnustep-base's suite cross-built against our Foundation with a published pass rate; a scoped, honestly-costed plan for the full ObjC Foundation port (Â§5); `nix/pkgs/apple/swift-core.nix` and `swift-test.nix`; the 5.4/5.8/5.10 stdlib import-diff spike.
**Success:** objc4 suite green; gnustep-base â‰¥ 90% pass; `class-dump` runs in-guest and dumps a real dylib; an unmodified Apple-toolchain hello-world Swift binary runs in the VM.

### C2 â€” CoreGraphics/Quartz, headless

Skia (BSD-3) behind a `CGContext`-shaped API; `CGImage`, `CGPath`, `CGColorSpace`, `CGDataProvider`, `CGFont` plus CoreText glyph rasterisation; output to a PDGOP/PDSurface framebuffer. No windows yet â€” deliberately decoupled from the compositor.
**Deliverables:** `CoreGraphics.framework` with per-file provenance headers; a headless render-to-PNG test corpus with checksummed output; the first `docs/spec/` entries if any Tier B work proves necessary.
**Success:** a non-GUI program draws text, paths and images correctly; the coverage report shows CoreGraphics symbol coverage above 80% across the Tier-2 corpus.

### C3 â€” AppKit and the window bridge â€” first GUI launch

Cocotron (MIT) vendored at a recorded commit as the AppKit base, forward-ported and backed by our CoreGraphics; a CGS/WindowServer shim mapping `NSWindow` onto the native compositor (Wayland/wlroots preferred over X11); the `NSApplication` run loop on our CFRunLoop; event translation; NIB/XIB unarchiving. **This is the phase where `__objc_superrefs` becomes the work plan** â€” ivar layout and dispatch ordering for `NSView`, `NSWindow`, `NSResponder`, `NSDocument` and friends.
**Deliverables:** `AppKit.framework`; the `DYLD_INSERT_LIBRARIES` selector-trace shim (Â§2), with its traces merged into corpus profiles.
**Success:** **Sequel Pro 1.1.2 reaches `NSApplicationMain` and puts a window on screen with menus and a working table view.** Secondary: MacVim or Quicksilver launches from an independent codebase â€” one app working can be an accident, two cannot.

### C4 â€” Real applications and the version wall

Second and third GUI apps, plus per-application **compatibility profiles**: a Wine-style per-app config controlling the reported OS version. A modern app's `LC_BUILD_VERSION minos` will refuse to load, and every `if #available(macOS 13, *)` check keys off the reported version. `vtool`-style per-binary `minos` rewriting is the near-term escape hatch; raising the system version globally is a project-wide commitment that promises the full API surface of whatever version we claim, and must not be done casually. Swift platform overlays land here if the shipping question in Â§3 is answered affirmatively.
**Success:** Sublime Text 4 usable as an editor; the compat-profile mechanism documented and covered by tests; corpus-wide `launch_score` published as a tracked project metric.

---

## 8. Risk register

| # | Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| R1 | A contributor with leaked-Apple-source exposure works in the clean-room subtree | **Fatal** â€” permanent reputational asterisk; ReactOS lost years | Low, rising with visibility | Tier C rule written and dated before C2; per-commit DCO attestation; per-file provenance; quarantine procedure written in advance (mark files, exclude from build, rewrite from spec by a clean author â€” never freeze the whole project) |
| R2 | Selector coverage is the real work and dwarfs symbol coverage | High â€” 2â€“5Ã— schedule slip on AppKit | **Near-certain** | Three-axis metric from day one; corpus-wide ranked histogram drives ordering; runtime tracing complements static analysis |
| R3 | Cocotron targets the 10.5â€“10.6 Cocoa era; it is a skeleton, not a modern AppKit | High | High | Treat it as a starting skeleton with a clean provenance chain, not a solution; pin a base commit; expect substantial forward-porting; measure the gap with `coverage.sh` before committing |
| R4 | `minos` / availability version wall blocks modern apps regardless of framework coverage | High â€” caps the "real apps" goal | **Certain** | Per-app compatibility profiles as a first-class C4 subsystem; prefer low-`minos` corpus targets meanwhile |
| R5 | ObjC Foundation is a multi-engineer-year port that is currently unscoped | High â€” blocks Swift Foundation and much of Tier 1 | **Certain** | Scope it explicitly in C1 and state the cost honestly; do not let it hide inside "AppKit work" |
| R6 | LGPL creep â€” GNUstep becomes the de-facto AppKit core | Medium â€” permanent downstream obligation, contradicts the project's premise | Medium (path of least resistance) | Decision recorded now: Cocotron (MIT) vendored, GNUstep studied and at most dynamically linked, never core; CI check on the link graph |
| R7 | Trademark exposure â€” "OSX" reads as an Apple abbreviation, and openosx.com is a senior commercial user in the same field | Medium â€” a letter from the small company is likelier than one from Apple | Medium | Rename before public launch; ship the non-affiliation disclaimer; keep Apple names to functional ABI positions only (install names, class/selector names), never in user-facing branding |
| R8 | Apple SDK artifacts (headers, `.swiftinterface`) leak into the repo or CI cache | Medium â€” redistribution exposure | Medium | `requireFile` pattern only; CI grep for Apple SDK header fingerprints outside APSL-sourced directories; CI check for Apple-origin assets in the image |
| R9 | Effort spent on apps that teach nothing (Electron, Apple system apps) | Medium â€” pure waste | Medium | Corpus explicitly tiered and gated; Apple apps out of scope permanently, in writing |
| R10 | No shared cache means slow Swift/ObjC process launch; someone files it as a bug | Low | High | Document as expected: loose dylibs lose prebuilt closures and page sharing. Not a correctness issue |
| R11 | Patents â€” clean-room discipline does not cover them (independent invention is no defence) | Low for a hobby project | Low | Acknowledge honestly; do not claim the clean room covers it; avoid pixel-identical trade-dress recreation |

---

*This document is engineering analysis of public sources, not legal advice. The rename (R7) and the Cocotron-plus-Skia architecture decision are the two items worth a paid hour with an IP attorney before committing to publicly.*
