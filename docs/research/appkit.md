# AppKit Reimplementation for OpenOSX â€” Research Report

**Scope:** what to build OpenOSX's AppKit *from*, given real objc4 + CoreFoundation, an APSL/BSD image, and a hard clean-room constraint.

**Bottom line up front:** Fork **`darlinghq/darling-cocotron`** (MIT, ~3,282 commits, already carries AppKit + CoreGraphics + Onyx2D + CoreText + QuartzCore), strip its bundled ObjC runtime and CoreFoundation, rebase it on OpenOSX's real objc4/CF, and add a CGS-shaped backend seam onto the existing PDSurface/wlroots stack. Do **not** build on GNUstep. Do **not** write from scratch.

---

## 0. Two corrections to the project's stated starting position

Before the analysis â€” I checked the actual tree, and two premises in the brief need adjusting.

### 0.1 OpenOSX does **not** have a real Foundation

`origin/openosx-next:src/Libraries/Foundation` is a six-class stub under **MPL-2.0**:

```
Collections.subproj/NSArray.{h,m}
Collections.subproj/NSDictionary.{h,m}
Runtime.subproj/NSError.{h,m}, NSObject.h, NSObjCRuntime.h, NSRange.h
String.subproj/NSString.{h,m}, NSCFString.{h,m}, NSAttributedString.h
URL.subproj/NSURL.{h,m}
```

That's it. There is no `NSBundle`, no `NSCoder`/`NSKeyedUnarchiver`, no `NSRunLoop`, no `NSNotificationCenter`, no `NSTimer`, no `NSDate`, no `NSFileManager`, no `NSValue`/`NSNumber`, no `NSOperation`, no `NSThread`. **AppKit cannot exist on top of this.** The Foundation gap is larger than the AppKit gap in terms of "what blocks the first real `.app`", because nib loading *is* `NSKeyedUnarchiver` + `NSBundle`.

The good news: the presence of `NSCFString.m` shows the intended architecture is correct â€” Foundation classes as CF-backed toll-free-bridged shells over the real CoreFoundation 1338. Keep that architecture; it is what real macOS binaries require.

**Consequence for this report:** the AppKit decision and the Foundation decision must be made *together*, and that strongly favors Cocotron, which ships both.

### 0.2 There is a `libobjc2` in the tree, and it is a hazard

`tools/cctools/libobjc2/CMakeLists.txt` exists on `openosx-dev` and contains `-DGNUSTEP -fobjc-runtime=gnustep-1.7`. OpenOSX's whole value proposition is the **authentic Apple objc4** runtime â€” Mach-O ObjC metadata, `objc_msgSend` from `libobjc.A.dylib`, non-fragile ivars, `_OBJC_CLASS_$_` mangling. Real macOS `.app` binaries are compiled `-fobjc-runtime=macosx` and will not talk to libobjc2 (see [gnustep/libobjc2#306](https://github.com/gnustep/libobjc2/issues/306) â€” objc4 splits a class into `objc_class`/`class_ro_t`/`class_rw_t` and the layouts are structurally incompatible).

**Rule to enforce project-wide:** nothing that links into the shippable image may depend on libobjc2. This single constraint eliminates most of the GNUstep option on its own.

---

## 1. Cocotron â€” deep dive

### 1.1 What it is

The Cocotron ([cocotron.org](https://www.cocotron.org/), [github.com/cjwl/cocotron](https://github.com/cjwl/cocotron)) is a from-scratch, **MIT-licensed** implementation of the Cocoa API surface: Foundation, AppKit, CoreGraphics, CoreFoundation, plus its own ObjC runtime, CoreData, PDFKit, OpenGL glue, Security stubs. 420 stars, 2,773 commits, 116 forks. It was written by Christopher Lloyd from **public Cocoa documentation and headers** â€” this is exactly the clean-room provenance OpenOSX needs, and it is the single most important reason to prefer it.

### 1.2 Completeness

AppKit is the most complete open-source AppKit reimplementation that exists. Directory survey of `AppKit/` shows real implementations (not stubs) of:

- **App/window/view core:** `NSApplication`, `NSWindow`, `NSView`, `NSResponder`, `NSScreen`, `NSPanel`, `NSDrawer`, `NSClipView`, `NSScrollView`
- **Cells & controls:** `NSCell`, `NSControl`, `NSActionCell`, `NSButton`/`NSButtonCell`, `NSTextField`, `NSComboBox`/`NSComboBoxCell`, `NSBrowser`, `NSBox`, `NSColorWell`, `NSSlider`, `NSTableView`, `NSOutlineView`
- **Menus & toolbars:** `NSMenu.subproj`, `NSToolbar.subproj`
- **Text:** `NSTextView.subproj` including `NSLayoutManager`, `NSTypesetter`, `NSTextStorage`
- **Graphics:** `NSBezierPath`, `NSAffineTransform`, `NSBitmapImageRep`, `NSColorSpace`, `NSImage`
- **Events:** `NSEvent.subproj`
- **Bindings:** `NSKeyValueBinding.subproj` (Cocoa Bindings â€” genuinely present)
- **Nibs:** `nib.subproj` (see Â§5)
- **Backends:** `Win32.subproj`, `X11.subproj`

Foundation is likewise broad: `NSBundle`, `NSRunLoop`, `NSThread`, `NSNotification`/`NSNotificationCenter`/`NSNotificationQueue`, `NSKeyedArchiving`, `NSArchiver`, `NSDate`, `NSTimer`, `NSFileManager`, `NSOperation`, `NSScanner`, `NSCharacterSet`, `NSException`, `NSIndexSet`, `NSValue`, `NSSet`, with `platform_darwin` / `platform_linux` / `platform_windows` split-outs. Cocotron already has a `platform_darwin` â€” that is not nothing.

### 1.3 Architecture

Three layers, cleanly separated, which is why it is forkable:

```
AppKit  â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
   â”‚ drawing goes through CoreGraphics only
   â–¼
CoreGraphics (thin CG* API shim)
   â”‚ every CG function wraps an O2* function
   â–¼
Onyx2D  ("O2Graphics") â€” self-contained Quartz2D replacement
   â”‚  O2Context subclasses select the device
   â”œâ”€â”€ O2Context_gdi          (Windows, HWND/HDC)
   â”œâ”€â”€ O2Context_builtin      (internal software rasterizer, zero deps)
   â””â”€â”€ O2Context_AntiGrain    (optional AGG 2.4 rasterizer, faster)
```

Onyx2D is fully self-contained â€” it even does PDF parse/read/write itself, and the rasterizer is internal code with no pixman/cairo dependency. Reported performance: the builtin rasterizer is "a little faster than pixman+cairo but way slower than Quartz 2D"; AGG is the escape hatch. AGG 2.4 as bundled is **3-clause BSD / AGG Public License** â€” permissive, shippable. (AGG *2.5* is GPL â€” same code, different license. Make sure any AGG you pull is 2.4/2.6-lineage from SourceForge, not 2.5.)

The backend abstraction is at **two** levels: Onyx2D `O2Context` subclasses for rasterization/compositing, and `AppKit/*.subproj` platform classes (`NSWindow_win32`, `NSWindow_X11`, event pumps, cursor, pasteboard, fonts) for windowing. This is the seam OpenOSX plugs into.

### 1.4 Maintenance state â€” and the fork you should actually use

Upstream `cjwl/cocotron` is **dormant**: substantial historical work, effectively no current development. Ignore it as a base.

**`darlinghq/darling-cocotron` is the live fork and is materially better**: ~3,282 commits (â‰ˆ500 ahead of upstream), MIT, and its tree has grown directories upstream does not have â€” **`CoreText`, `QuartzCore`, `CoreData`, `Onyx2D` with libpng/libjpeg/libz linkage**. It also has open PRs and an architectural roadmap. Note `Onyx2D/O2Encoder_PNG.m` and the commit "Enable linking to libpng, libjpeg, and libz" â€” image I/O that upstream lacks.

Crucially, Darling has already written down the correct architecture for exactly OpenOSX's problem, in [darling#937 "Cocotron (AppKit) backend rework"](https://github.com/darlinghq/darling/issues/937):

- **Remove** backends from `AppKit/*.backend`; **add** backends in `CoreGraphics/*.backend` implementing the **CGS** (CoreGraphics Services / window-server) interfaces
- X11 and Wayland backends, XInput2 for events
- Event lifecycle: X11/Wayland event â†’ `CGSEventRecord` â†’ posted to a Mach port obtained via `CGSGetEventPort()` â†’ `CGEventCreateNextEvent()` â†’ `CGEventRef` â†’ Carbon `EventRef` â†’ back to `CGSEventRecord` â†’ `NSEvent`
- Stated invariant: *"All events must exist as CGSEventRecords prior to becoming CGEvents and later NSEvents."*

That last point matters more than it looks: apps use `CGEventTap`, `CGEventCreateMouseEvent`, `NSEvent +addGlobalMonitor...`, and Carbon `EventRef` paths. If your event objects are born as `NSEvent`, all of those are dead ends. **Adopt the CGS-first event pipeline from day one** â€” retrofitting it later is a rewrite.

### 1.5 What must be modernized â€” the actual work list

This is the honest cost of the Cocotron path.

| # | Issue | Severity | Work |
|---|---|---|---|
| 1 | **Ships its own ObjC runtime** (`objc/`) | Critical | **Delete it.** Build all frameworks `-fobjc-runtime=macosx` against OpenOSX's objc4. Cocotron code is ObjC 1.0-era in places: manual retain/release (fine â€” keep MRR, do *not* convert to ARC), but expect `@synthesize` gaps, `Class`/`isa` direct pokes, and `objc_msgSend` prototype casts to fix. |
| 2 | **Ships its own CoreFoundation** | Critical | Delete; use real CF 1338. Then the real work: make Cocotron's `NSString`/`NSArray`/`NSDictionary`/`NSData`/`NSNumber` **toll-free bridged** to `CFStringRef` et al. OpenOSX's existing `NSCFString.m` is the template. This is the single largest Foundation task and it is unavoidable for binary compat â€” real apps pass `NSString*` into `CFStringGetCString()` constantly. |
| 3 | **32-bit-era assumptions** | High | Cocotron's own site historically targeted i386/Win32. Audit for `int`-vs-`NSInteger`, `float`-vs-`CGFloat` (AppKit on 64-bit uses `double` CGFloat), `unsigned`-vs-`NSUInteger` in method signatures. Method *signatures* must match Apple's 64-bit ABI exactly or every subclass override in a real app mis-binds. Mechanically checkable against public SDK headers. |
| 4 | **ARC** | Medium | Cocotron's runtime had only a trivial ARC shim (no weak refs). Once on objc4 this evaporates â€” objc4 provides `objc_retainAutoreleasedReturnValue`, `objc_storeWeak`, etc. natively. Cocotron's own sources stay MRR; *client apps* get full ARC for free. |
| 5 | **Blocks** | Medium | Long-standing Cocotron gap. objc4 + `libclosure` from OpenOSX's libSystem solves it, but Cocotron's APIs predate block-taking methods (`-enumerateObjectsUsingBlock:`, `NSOperationQueue -addOperationWithBlock:`, `+[NSNotificationCenter addObserverForName:...usingBlock:]`). These must be *added* â€” real apps use them everywhere. |
| 6 | **X11 backend is weak** | High | Upstream states plainly: AppKit runs on **Windows**; X11 "is present for Linux/FreeBSD but needs a lot of work." Budget for writing the backend, not porting one. |
| 7 | **No CoreAnimation / layer-backed views** | High | `QuartzCore` exists in darling-cocotron but is early. Modern apps set `view.wantsLayer = YES`. Minimum: `CALayer` as a retained-backing-store `NSView` shadow, no implicit animation, correct `-drawRect:` fallthrough. |
| 8 | **Text/font stack** | High | Cocotron's font path was GDI-backed (`O2Font_gdi`) or FreeType. Needs a real FreeType+HarfBuzz+fontconfig path and a `CoreText` API layer (darling-cocotron has a start). Its `NSLayoutManager`/`NSTypesetter` are TextKit 1-era, which is *correct* â€” AppKit's own default is still TextKit 1 semantics. |
| 9 | **Modern AppKit classes absent** | Medium | `NSStackView`, `NSSplitViewController`, `NSVisualEffectView`, `NSCollectionView`, `NSViewController`-based containment, Auto Layout (`NSLayoutConstraint` + a Cassowary solver). Auto Layout is a real subproject; every storyboard app needs it. |
| 10 | **Symbol export completeness** | Critical | See Â§4.4. |

**Provenance check before merging anything:** darling-cocotron mixes Darling-authored files into an MIT tree. Run a per-file license header audit (`LICENSE` says MIT, but verify `CoreText/`, `QuartzCore/`, `CoreData/` headers individually) and record the result in `docs/`. Also confirm no file carries Apple headers beyond what the public SDK provides.

---

## 2. GNUstep AppKit (`libs-gui` / `libs-back`)

### 2.1 Completeness

GNUstep `libs-gui` is mature and in some respects *more* polished than Cocotron: 30 years of work, current stable **gui 0.32.0 / back 0.32.0 (Feb 2025)**, and â€” importantly â€” **it reads modern XIBs**. Since gui 0.28.0 it interprets XIB files produced by recent Xcode (Xcode 11-era at time of writing), via `GSXib5KeyedUnarchiver`, and Cocoa Bindings work "to some extent". There's also `gnustep/tools-nib2xib` for legacy typedstream nibs.

Its backend model: `libs-back` provides swappable backends behind a `GSContext`/DPS-derived drawing abstraction â€” **cairo is the default**, with X11 and an **alpha Wayland backend since back 0.29.0**. Windowing goes through `GSDisplayServer` (X11 / Wayland / Win32).

### 2.2 Why it's still the wrong base for OpenOSX

Four disqualifiers, in order of severity:

1. **It requires gnustep-base, not CoreFoundation.** `libs-gui` is written against `gnustep-base`'s Foundation. OpenOSX's Foundation must be CF-backed with toll-free bridging (real apps depend on it). You cannot have both `gnustep-base`'s `NSString` and a `CFString`-backed `NSString` in one process. Replacing gnustep-base under libs-gui is a larger job than porting Cocotron's AppKit.
2. **It expects libobjc2.** Which OpenOSX must not ship (Â§0.2). GNUstep *can* build against Apple's runtime, but its idioms and much of `base` assume libobjc2 extensions.
3. **Drawing model mismatch.** GNUstep draws through a DPS/PostScript-derived `GSContext`, not CoreGraphics. OpenOSX needs a genuine `CGContextRef` because apps call CG directly â€” `CGContextDrawImage`, `CGPathCreateMutable`, `NSGraphicsContext.CGContext`. Cocotron's "AppKit â†’ CG â†’ Onyx2D" layering is *already* the shape you need; GNUstep's is not.
4. **Not ABI-shaped.** GNUstep aims at *source* compatibility with Cocoa. OpenOSX needs *binary* compatibility â€” exact class names, exact 64-bit method signatures, exact framework install names. Nothing in GNUstep is oriented that way.

### 2.3 License implications

`libs-gui` and `libs-back` are **LGPL-2.1** (libraries and library resources); GNUstep *tools and test programs* are **GPL-3.0**. Practical reading for an APSL/BSD image:

- **Shippable? Technically yes, with conditions.** LGPL-2.1 Â§6 is satisfied by dynamic linking against a replaceable `.dylib`. An OS image shipping `AppKit.framework` as an LGPL dylib is legally fine *provided* you ship the LGPL source and permit relinking. On a Darwin image where the framework is loaded by dyld from a normal path, relinking is satisfiable.
- **But:** any change *you* make to `libs-gui` is a derivative work and must be LGPL. A system framework that everything in the OS links against, permanently under a copyleft distinct from the rest of the image, is a governance problem â€” it fragments the image's license story and it's the kind of thing that scares downstream packagers and any future commercial redistributor.
- **And the killer for contribution:** *"Larger patches require copyright assignment to FSF."* OpenOSX would be doing enormous work it cannot upstream without assigning copyright to the FSF, and cannot relicense.
- Never link GNUstep **tools** (GPL-3.0) into anything shipped.

**Verdict: study freely, vendor nothing.** The one thing worth *studying* hard is `GSXib5KeyedUnarchiver` â€” it's the best open account of how modern XIB object graphs decode. Study the *behavior*, write your own.

---

## 3. Darling â€” what it actually does, and the lessons

### 3.1 Current state

Darling is a Mach-O loader + `darlingserver` (userspace Mach IPC / Darwin syscalls) + reimplemented frameworks. On GUI, be clear-eyed. From a Darling collaborator, **9 Feb 2025**, in [discussion #1563](https://github.com/darlinghq/darling/discussions/1563):

> "Darling can run some really basic (hello world) GUI apps, but will likely fail to run any GUI app that more complicated beyond that."

As of March 2025 assessments: GUI support remains "basic and of limited practical utility"; CLI tools work well; the [known non-functional software](https://docs.darlinghq.org/known-nonfunctional-software.html) page is long. There is an initial **Metal backend via Vulkan translation** in progress.

**This is after ~13 years of work by a competent team.** Calibrate expectations accordingly â€” but note *why* it's slow, because OpenOSX's situation differs favorably (Â§3.3).

### 3.2 The three-repo mess (and which one is real)

| Repo | What | License | Status |
|---|---|---|---|
| **`darling-cocotron`** | Cocotron fork: AppKit, CoreGraphics, Onyx2D, CoreText, QuartzCore, CoreData | **MIT** | **The real one.** ~3,282 commits, active, 11 open PRs |
| `darling-appkit` | Qt-backed AppKit experiment: `NSApplication.mm`, `NSWindow.mm`, `QNSEventDispatcher` | **GPL-3.0** | Dead â€” **4 commits**, 9 stars. Abandoned |
| `darling-appkit-gui` | Fork of GNUstep `libs-gui` | LGPL | Historical, superseded |
| `darling-foundation` | Foundation, "derived from Apportable Foundation", previously gnustep-base, now "Apportable Cocotron and some others" | **LGPL-2.1** | 318 commits |

The trajectory is itself the lesson: Darling tried **GNUstep gui â†’ Qt â†’ Cocotron**, and landed on Cocotron. Somebody already ran this experiment for you. Take the answer.

### 3.3 Lessons for OpenOSX

1. **Take darling-cocotron (MIT). Avoid darling-appkit (GPL-3.0) and darling-foundation (LGPL-2.1)** â€” the exact license split your brief demands.
2. **Most of Darling's cost is not AppKit.** It's `darlingserver`, Mach-on-Linux, the Mach-O loader, dyld emulation, and Darwin syscall shims. **OpenOSX pays none of that** â€” it runs the real XNU with real Mach ports, real dyld, real launchd. OpenOSX's AppKit effort is a genuinely smaller project than Darling's. This is the strongest argument that this is tractable.
3. **Their architecture verdict (issue #937) is correct: the backend seam belongs at CGS, under CoreGraphics â€” not inside AppKit.** Cocotron's original design (backends in `AppKit/*.subproj`) is the thing to change first, before writing OpenOSX-specific code, or you'll write it twice.
4. **Themes drawn directly on CoreGraphics/Onyx2D inside AppKit** is Darling's approach (see [darling#369](https://github.com/darlinghq/darling/issues/369)) â€” relevant to the "Aqua translation" goal and to the XFCE-fork identity DE, since it means control appearance is your code, not a system theme engine.

---

## 4. Minimum viable AppKit to launch one real `.app`

### 4.1 Tier 0 â€” process gets to `main()` without dyld dying

Before a single pixel: dyld must resolve every symbol the app imports. See Â§4.4.

### 4.2 Tier 1 â€” the classes essentially every AppKit app touches

`NSApplication` (+`NSApplicationMain`, delegate protocol, `-run`, `-sendEvent:`, `-terminate:`, activation policy) Â· `NSResponder` Â· `NSWindow` Â· `NSView` Â· `NSEvent` Â· `NSScreen` Â· `NSColor` Â· `NSFont` Â· `NSImage` Â· `NSGraphicsContext` Â· `NSBezierPath` Â· `NSMenu`/`NSMenuItem` Â· `NSNib`/`NSBundle -loadNibNamed:owner:topLevelObjects:` Â· `NSPasteboard` Â· `NSCell`/`NSControl` Â· `NSButton` Â· `NSTextField` Â· `NSWorkspace` (stubbable) Â· `NSCursor` Â· `NSAlert` Â· `NSUserDefaults` (Foundation, but AppKit assumes it)

Plus, from Foundation, hard blockers: `NSBundle`, `NSKeyedUnarchiver`/`NSCoder`, `NSRunLoop`, `NSNotificationCenter`, `NSDate`/`NSTimer`, `NSFileManager`, `NSValue`/`NSNumber`, `NSThread`, `NSOperationQueue`.

### 4.3 Tier 2 â€” needed for a real, non-toy app

Auto Layout (`NSLayoutConstraint`, `NSLayoutAnchor`, a Cassowary solver) Â· `NSViewController`/`NSWindowController` Â· `NSScrollView`/`NSClipView` Â· `NSTextView`+`NSLayoutManager`+`NSTextStorage` Â· `NSTableView`/`NSOutlineView` Â· `NSToolbar` Â· `NSSavePanel`/`NSOpenPanel` Â· `NSDocument`/`NSDocumentController` Â· `NSStoryboard` Â· `NSSegmentedControl`, `NSPopUpButton`, `NSSlider`, `NSProgressIndicator`, `NSStackView` Â· `NSAppearance` Â· Cocoa Bindings (Cocotron has `NSKeyValueBinding.subproj`) Â· `CALayer` for `wantsLayer`

### 4.4 The gate nobody plans for: **symbol export completeness**

A real `.app` is dynamically linked against `/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit`. dyld resolves **every** imported symbol at load. One missing `_OBJC_CLASS_$_NSVisualEffectView` or `_NSAccessibilityPostNotification` and the process dies before `main()` â€” regardless of whether the app would ever have used it.

So "minimum viable" is not "minimum implemented" â€” it is **complete exports, partial implementations**.

**The legitimate mechanism:** Apple ships **`.tbd` text-based stub files** in the public SDK. These are the canonical public export list â€” TAPI's format explicitly includes `objc-classes` (exported/undefined ObjC class names, listed separately to dodge ABI mangling differences) and `objc-ivars`. v1â€“v4 are YAML, v5+ is JSON, both trivially machine-readable, and LLVM's `lib/TextAPI/TextStub.cpp` parses them.

Concretely:
1. Parse `MacOSX11.3.sdk/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit.tbd`.
2. Emit a complete stub `.m`/`.c` set: every class as `@interface X : NSObject @end` with `+load`-time unimplemented-selector trapping, every C symbol as a function that logs and aborts.
3. Implement over the stubs, tier by tier. Coverage becomes measurable: *"AppKit exports N symbols; M are real; K trapped at runtime by the test corpus."*
4. Same treatment for `CoreGraphics.tbd`, `CoreText.tbd`, `QuartzCore.tbd`, `Foundation.tbd`.

`.tbd` files are declaration-only public SDK artifacts, on the same clean-room footing as the public headers OpenOSX already builds against. This is safe *and* it converts an unbounded problem into a burndown chart. Build this tool first.

### 4.5 The "hello, real app" test corpus

Pick apps that are (a) small, (b) AppKit-only, (c) distributed as plain unsigned/ad-hoc-signed Mach-O. Old open-source Cocoa apps built with legacy nibs are the ideal first targets â€” they exercise real nib loading without dragging in Metal, AVFoundation, or Swift. Track pass/fail in CI alongside the boot harness.

---

## 5. Nib / XIB / Storyboard â€” the hard dependency

This is correctly identified as a gate. Real apps do not construct UI in code; they unarchive it. The situation is better than feared.

### 5.1 There are **two** compiled formats, and you need both

**(a) Cocoa Keyed Archive** â€” the classic macOS format. A `Foo.nib` is a *directory* containing `keyedobjects.nib` (and often version-suffixed siblings like `keyedobjects-101300.nib`), each a **binary plist written by `NSKeyedArchiver`**. Fully documented public API; the object graph is `NSIBObjectData` holding `NSCustomObject`, `NSCustomView`, `NSWindowTemplate`, `NSClassSwapper`, and connector objects (`NSNibOutletConnector`, `NSNibControlConnector`, `NSNibBindingConnector`).

**Cocotron already parses this.** `AppKit/nib.subproj` contains exactly those classes: `NSNib`, `NSNibLoading`, `NSIBObjectData`, `NSCustomObject`, `NSCustomView`, `NSCustomResource`, `NSWindowTemplate`, `NSClassSwapper`, `NSButtonImageSource`, `NSNibConnector`, `NSNibOutletConnector`, `NSNibControlConnector`, `NSNibBindingConnector`, `NSNibHelpConnector`, `NSNibAXRelationshipConnector`. This is a substantial, already-written asset â€” arguably the single highest-value thing you inherit by forking Cocotron.

**(b) NIBArchive (`UINibEncoder`)** â€” the compact format. Originally UIKit-only (iOS 6+), **but as of Xcode 13, macOS storyboards and XIBs also compile with `UINibEncoder`**. Any app built in the last ~5 years ships this format. You need it.

### 5.2 NIBArchive format â€” fully documented, open parsers exist

Publicly reverse-engineered and written up, with no Apple source involved:

- **[nibsqueeze/NibArchive.md](https://github.com/matsmattsson/nibsqueeze/blob/master/NibArchive.md)** â€” the canonical format writeup
- **[Mothersruin Archaeology â€” UIKit NIB Archives](https://www.mothersruin.com/software/Archaeology/reverse/uinib.html)** â€” the most rigorous treatment; also the source for the Xcode 13 macOS note
- **[MatrixEditor/nibarchive](https://github.com/MatrixEditor/nibarchive)** (Python) and **[michaelwright235/nibarchive](https://github.com/michaelwright235/nibarchive)** (Rust) â€” working parsers
- **[mandiant/macOS-tools `nib_parse.py`](https://github.com/mandiant/macOS-tools/blob/master/nib_parse.py)** â€” decodes nibs and dumps connections
- `szhu/editnib`, `dkimitsa`'s **xib2nib** (RoboVM) â€” an *encoder*, useful as a differential-testing oracle

Format: 50-byte header, magic `"NIBArchive"`, then `_formatVersion`, `_coderVersion`, and count/offset pairs for four tables â€” **objects, keys, values, class names** â€” with cross-references by array index rather than pointer. Integers are **VInt32**: 7 bits per byte, little-endian, high bit marks the last byte. Values carry types (int8/16/32/64, float, double, bool, nil, object ref, raw bytes). Foundation collections are **inlined** rather than keyed-archived normally, using sentinel keys like `UINibEncoderEmptyKey` â€” this is the main gotcha.

**Implementation plan:** write `NSNibArchiveUnarchiver` (an `NSCoder` subclass) in ObjC from the published format spec, and make `NSNib` sniff the magic to dispatch between it and `NSKeyedUnarchiver`. This is a bounded, well-specified, ~2â€“4 week task with reference implementations to test against. **It is not the hard part** â€” the hard part is that decoding succeeds and then instantiates 400 AppKit classes that must all behave correctly.

### 5.3 Storyboards

`.storyboardc` is a **directory** of `.nib` files plus a **binary `Info.plist`** that maps identifiers to nib names. On iOS the key is `UIViewControllerIdentifiersToNibNames`; macOS uses the AppKit-prefixed analogues, plus an initial-controller identifier. So storyboard support = nib support + `NSStoryboard` + `NSViewController`/`NSWindowController` + segues. Sequence it *after* nibs; it's genuinely incremental.

### 5.4 The strategic point about nibs

Nib decoding is the **conformance test that drives everything else**. A nib names every class, sets every property via `-initWithCoder:`, and wires every outlet/action. Get `NSKeyedUnarchiver` + `nib.subproj` running against a real app's nib and the failure log becomes a *prioritized, app-derived worklist* for AppKit â€” far better than guessing at a class list. **Make "decode a real app's MainMenu.nib and log every unimplemented class/key" the very first AppKit milestone.**

---

## 6. Recommendation

### Decision: **fork `darlinghq/darling-cocotron`, rebase on real objc4/CF, re-seam at CGS.**

Not GNUstep. Not from scratch.

**Why Cocotron wins on every axis that matters here:**

| Criterion | Cocotron (darling fork) | GNUstep | From scratch |
|---|---|---|---|
| License into APSL/BSD image | **MIT â€” ideal** | LGPL-2.1 + FSF assignment | n/a |
| Clean-room provenance | **Written from public docs** | Clean but OpenStep-spec lineage | Clean |
| Coexists with real objc4 | **Yes, after removing its runtime** | Wants libobjc2 â€” blocker | Yes |
| Coexists with real CoreFoundation | Yes, after bridging work | **No â€” gnustep-base conflict** | Yes |
| Drawing model | **AppKitâ†’CGâ†’Onyx2D, already right** | DPS/GSContext â€” wrong shape | Yours |
| Ships a Foundation you need | **Yes â€” and you have almost none** | Yes but wrong-licensed/wrong-based | No |
| Nib loading already present | **Yes (keyed archive)** | Yes (XIB5) but LGPL | No |
| Time to first real `.app` | ~12â€“18 months | Longer (rip out base first) | Many years |

**The tiebreaker is Â§0.1.** OpenOSX's Foundation is six classes. Cocotron hands you a complete MIT Foundation *and* AppKit *and* CoreGraphics *and* a nib loader, all designed to layer together. Nothing else on the table does that. And Darling already tried GNUstep and Qt before settling on Cocotron â€” that's a paid-for experimental result, take it.

**Why not from scratch:** the API surface is ~4,000 exported symbols across AppKit alone. The clean-room constraint doesn't require you to write it yourself; it requires that what you write not derive from Apple source. MIT Cocotron satisfies that completely and saves a decade.

**The one place "from scratch" is right:** the **backend layer** (CGS â†” compositor). Cocotron's Win32/X11 backends are a Windows-first design with a weak X11 side ("needs a lot of work"). Don't port them â€” replace them with an OpenOSX-native CGS backend over the existing PDSurface/PDGOP/PDVirglShim/wlroots stack.

### Target architecture

```
       real macOS .app (Mach-O x86_64, unmodified)
                        â”‚
   AppKit.framework  â—„â”€â”€ forked Cocotron AppKit, exports complete per AppKit.tbd
                        â”‚
   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¼â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
   â”‚                    â”‚                     â”‚
Foundation          CoreGraphics          CoreText / QuartzCore
(Cocotron classes,  (CG API shim â†’         (FreeType + HarfBuzz;
 CF-bridged onto     Onyx2D rasterizer)     CALayer over CG)
 real CF 1338)           â”‚
   â”‚                     â”‚
   â””â”€â”€â”€â”€ real objc4 â”€â”€â”€â”€â”€â”˜
                         â”‚
              CGS backend (NEW â€” OpenOSX-authored)
              windows â€¢ surfaces â€¢ CGSEventRecord
                         â”‚
        â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”´â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
   PDSurface / wlroots               X11 / Xorg
        â”‚
   PDGOP framebuffer / PDVirglShim / Mesa
```

Note the CGS layer is *also* where "Aqua translation onto the native compositor" lives, and where the forked XFCE DE meets AppKit. One seam, three project goals.

### Sequenced plan

**Phase 0 â€” decide and instrument (4â€“6 weeks)**
- Vendor `darling-cocotron` under `src/Libraries/` with a squashed-subtree commit, matching the existing `tools/kc-tools` / `tools/xnu-loader` vendoring pattern
- Per-file license audit; record in `docs/`
- **Build the `.tbd` â†’ stub-symbol generator.** Emit trapping stubs for AppKit/CoreGraphics/CoreText/QuartzCore/Foundation; wire a coverage report into CI
- Write down the rule: nothing links libobjc2

**Phase 1 â€” Foundation on real CF (3â€“5 months)** *â€” the actual critical path*
- Delete Cocotron's `objc/` and `CoreFoundation/`; build everything `-fobjc-runtime=macosx`
- Toll-free-bridge `NSString`/`NSArray`/`NSDictionary`/`NSData`/`NSNumber`/`NSDate` onto CF 1338, extending the existing `NSCFString.m`
- 64-bit signature audit against public SDK headers (`NSInteger`/`NSUInteger`/`CGFloat`)
- Add block-taking APIs
- **Gate:** `NSKeyedUnarchiver` round-trips a real app's `keyedobjects.nib` and logs unimplemented classes

**Phase 2 â€” CGS backend + first window (4â€“6 months)**
- Implement the Â§3.1/#937 event pipeline: compositor event â†’ `CGSEventRecord` â†’ Mach port â†’ `CGEventRef` â†’ `NSEvent`
- Onyx2D `O2Context` targeting PDSurface; AGG 2.4 as the fast path
- FreeType/HarfBuzz/fontconfig behind `CoreText`
- **Gate:** hand-written `NSApplication` + `NSWindow` + `NSView -drawRect:` app draws on the OpenOSX desktop

**Phase 3 â€” nib loading end to end (3â€“4 months)**
- `NSNibArchiveUnarchiver` from the published spec; magic-sniffing dispatch in `NSNib`
- Differential-test against the Python/Rust parsers and xib2nib
- Burn down the class list the decoder logs
- **Gate:** a real, unmodified third-party `.app` opens its main window with a working menu bar

**Phase 4 â€” breadth**
- Auto Layout, `NSTableView`/`NSOutlineView`, TextKit, `NSDocument`, storyboards, `CALayer`/`wantsLayer`, Aqua-ish theme drawn on CG, pasteboard/drag-and-drop against the compositor

### Explicit do-nots

- **Do not** vendor `darling-appkit` (GPL-3.0) or `darling-foundation` (LGPL-2.1) â€” study only
- **Do not** vendor any GNUstep code â€” study `GSXib5KeyedUnarchiver`'s *behavior*, write your own
- **Do not** convert Cocotron to ARC; keep MRR and let client apps use ARC
- **Do not** pull AGG 2.5 (GPL); AGG 2.4 / 2.6-lineage only
- **Do not** put backends in `AppKit/*.subproj` â€” CGS from day one
- **Do not** read leaked Apple source. Public SDK headers + `.tbd` files + published RE writeups + MIT/BSD reimplementations only. Consider a written contributor attestation, ReactOS-style, before the first AppKit commit lands

---

## Sources

- [The Cocotron](https://www.cocotron.org/) Â· [MIT license page](https://cocotron.com/Info/The_MIT_License) Â· [System Requirements](https://cocotron.com/Using/System_Requirements)
- [cjwl/cocotron](https://github.com/cjwl/cocotron) Â· [AppKit tree](https://github.com/cjwl/cocotron/tree/master/AppKit) Â· [nib.subproj](https://github.com/cjwl/cocotron/tree/master/AppKit/nib.subproj) Â· [Foundation tree](https://github.com/cjwl/cocotron/tree/master/Foundation)
- [darlinghq/darling-cocotron](https://github.com/darlinghq/darling-cocotron) Â· [darling-appkit](https://github.com/darlinghq/darling-appkit) Â· [darling-appkit-gui](https://github.com/darlinghq/darling-appkit-gui) Â· [darling-foundation](https://github.com/darlinghq/darling-foundation)
- [darling#937 â€” Cocotron (AppKit) backend rework](https://github.com/darlinghq/darling/issues/937) Â· [darling#369 â€” Theming support](https://github.com/darlinghq/darling/issues/369) Â· [darling#39 â€” Use of Cocotron](https://github.com/darlinghq/darling/issues/39)
- [darling discussion #1563 â€” current GUI support status](https://github.com/darlinghq/darling/discussions/1563) Â· [Known non-functional software](https://docs.darlinghq.org/known-nonfunctional-software.html) Â· [Darling (Wikipedia)](https://en.wikipedia.org/wiki/Darling_(software))
- [gnustep/libs-gui](https://github.com/gnustep/libs-gui) Â· [libs-back](https://github.com/gnustep/libs-back) Â· [libs-back NEWS](https://github.com/gnustep/libs-back/blob/master/NEWS) Â· [GNUstep Backend wiki](https://mediawiki.gnustep.org/index.php?redirect=no&title=Backend) Â· [GNUstep XIB wiki](https://mediawiki.gnustep.org/index.php/XIB) Â· [tools-nib2xib](https://github.com/gnustep/tools-nib2xib)
- [gnustep/libobjc2](https://github.com/gnustep/libobjc2) Â· [issue #306 â€” objc4 class structure incompatibilities](https://github.com/gnustep/libobjc2/issues/306) Â· [ObjC2 FAQ](https://mediawiki.gnustep.org/index.php/ObjC2_FAQ)
- [nibsqueeze â€” NibArchive.md](https://github.com/matsmattsson/nibsqueeze/blob/master/NibArchive.md) Â· [Mothersruin Archaeology â€” UIKit NIB Archives](https://www.mothersruin.com/software/Archaeology/reverse/uinib.html) Â· [MatrixEditor/nibarchive](https://github.com/MatrixEditor/nibarchive) Â· [michaelwright235/nibarchive (Rust)](https://docs.rs/nibarchive) Â· [mandiant nib_parse.py](https://github.com/mandiant/macOS-tools/blob/master/nib_parse.py) Â· [szhu/editnib](https://github.com/szhu/editnib) Â· [xib2nib writeup](https://dkimitsa.github.io/2018/02/13/wl-tech-details-2-robovm/)
- [TAPI TBD.rst format spec](https://github.com/apple-opensource/tapi/blob/master/docs/TBD.rst) Â· [tapi(1)](https://keith.github.io/xcode-man-pages/tapi.1.html) Â· [LLVM TextStub.cpp](https://llvm.org/doxygen/TextStub_8cpp_source.html)
- [Anti-Grain Geometry license](https://agg.sourceforge.net/antigrain.com/license/index.html) Â· [AGG license discussion](https://sourceforge.net/p/agg/discussion/118993/thread/99fb7419de/)
- [Design of a multi-platform app using The Cocotron](https://cocoawithlove.com/2010/04/design-of-multi-platform-app-using.html) Â· [GNUstep vs Cocotron discussion](https://discuss-gnustep.gnu.narkive.com/FYxzPC90/gnustep-vs-the-cocotron-for-mac-to-windows-porting)
- [apportable/Foundation](https://github.com/apportable/Foundation) Â· [Lore-Hex/QuillUI](https://github.com/Lore-Hex/QuillUI) (MIT, Swift/Qt, source-recompile not binary â€” not applicable to OpenOSX's binary-compat goal, but worth watching)
- Local: `origin/openosx-next:src/Libraries/Foundation` (MPL-2.0, 6 classes), `tools/cctools/libobjc2/CMakeLists.txt`

