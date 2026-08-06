# OpenOSX Aqua / UI Plan

**Status:** authoritative engineering plan for the graphics stack, the Aqua translation layer, and the forked desktop.
**Scope:** everything between a real Mach-O `.app` calling `-[NSView drawRect:]` and a lit pixel.
**Clean-room:** binding. See Â§9 and `docs/CLEANROOM_POLICY.md`.

---

## 1. Executive summary

OpenOSX already runs the hard part â€” real XNU, real dyld, real objc4, real CoreFoundation 1338 â€” so the remaining gap is a graphics and UI stack, not an OS. We will **fork `darlinghq/darling-cocotron` (MIT)** for AppKit, Foundation, CoreGraphics and Onyx2D, delete its bundled ObjC runtime and CoreFoundation, and rebase it on ours; we will **not** write AppKit from scratch and we will **not** build on GNUstep. Rasterization goes through a pluggable `O2Context` backend with **Cairo (MPL-1.1 election)** as the shipping default and Onyx2D's built-in software rasterizer as the zero-dependency bring-up and no-GPU fallback. Between AppKit and the screen sits **`owsd`**, our own display server: our protocol, not Apple's private CGS wire format, with XPC for control, a Mach-signalled shared-memory ring for events, and **PDSurface** â€” which already exists in-tree â€” as the IOSurface equivalent. `owsd` ships first as a **Wayland client of the existing sway session**, then becomes the compositor itself on wlroots; Aqua chrome is drawn client-side by AppKit, never by a themed window manager. The honest bottom line: a window with correct pixels is months away, a real third-party app being genuinely usable is **years**, and Metal and DRM'd applications may never arrive.

---

## 2. DECISION: the layer cake

```
  real Mach-O x86_64 .app  (unmodified, two-level namespace, dyld-loaded)
        â”‚  LC_LOAD_DYLIB /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
        â–¼
  AppKit.framework          â† forked Cocotron AppKit. ZERO platform code.
  Foundation.framework      â† Cocotron Foundation, CF-bridged onto real CF 1338
        â”‚  all drawing via CGContextRef; all windowing via libOWSClient
        â–¼
  CoreGraphics.framework    â† thin ABI shim, exports complete per CoreGraphics.tbd
        â–¼
  Onyx2D (O2*)              â† object model: gstate stack, CTM, clip, PDF imaging semantics
        â”œâ”€â”€ O2Context_builtin   MIT, no deps      â€” bring-up, CI goldens, no-GPU fallback
        â”œâ”€â”€ O2Context_cairo     DEFAULT           â€” cairo-image / cairo-gl over Mesa
        â””â”€â”€ O2Context_skia      opt-in, post-G4   â€” GPU, behind the same vtable
        â–¼
  libOWSClient.dylib        â† the ONLY thing that talks to the display server
        â”‚  XPC control Â· Mach+shm event ring Â· PDSurface handles (pixels never copied)
        â–¼
  owsd  (OpenOSX WindowServer)
        â”œâ”€â”€ OWSBackendHeadless   CI â€” build this FIRST
        â”œâ”€â”€ OWSBackendWayland    G1â€“G4: client of the in-tree sway session
        â”œâ”€â”€ OWSBackendWlroots    G5: owsd IS the compositor
        â””â”€â”€ OWSBackendX11        fallback: Xorg 21.1.24 + forked xfwm4 (already boots)
        â–¼
  PDSurface â†’ libgbm/libdrm shims â†’ Mesa/virgl â†’ IOGOPFramebuffer / virtio-gpu
```

**AppKit substrate â€” forked `darling-cocotron`, MIT.** Primary. It carries AppKit, Foundation, CoreGraphics, Onyx2D, CoreText and QuartzCore in one tree designed to layer together, plus `AppKit/nib.subproj`, a working keyed-archive nib loader. Fallback: upstream `cjwl/cocotron` if the per-file provenance audit (Â§3) rejects too much of the Darling delta.

**CoreGraphics substrate â€” Onyx2D behind an ABI-exact CG shim.** Primary. This shape was independently rediscovered three times (Cocotron/Onyx2D, GNUstep/Opal, ravynOS); do not invent a fourth. ravynOS's `Frameworks/CoreGraphics` + `Frameworks/Onyx2D` are MIT-headered inside a BSD-2 tree and are the more actively maintained copy â€” pull from there where the two diverge.

**Rasterizer â€” Cairo, elect MPL-1.1.** Primary. Quartz 2D *is* the PDF 1.4 imaging model: its blend modes are the PDF blend modes, its shadings are PDF functions, `cairo_push_group()` is `CGContextBeginTransparencyLayer`, and `CAIRO_FORMAT_ARGB32` is byte-identical to `kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host` on little-endian x86_64 â€” a zero-copy path from bitmap context to compositor buffer. Cairo, FreeType, fontconfig, HarfBuzz and Mesa are already in the image, so this adds no build-system risk. Fallback and CI oracle: `O2Context_builtin`. Skia (BSD-3) is technically the better long-term rasterizer and WebKit's move to it is a real signal, but it has no stable API/ABI, a GN/Ninja vendored toolchain that must be cross-built to Darwin, and an `SkColorSpace` model narrower than `CGColorSpace`. **Sequence it after the golden-image suite exists** â€” the suite is what makes the swap safe, so build the suite against Cairo and cash it in against Skia.

**Compositor target â€” Wayland/wlroots strategic, hosted-sway tactical, X11 as fallback.** The strongest argument for X11-primary is that you can ship the compat layer before writing a compositor. That argument is satisfied without X11: **`OWSBackendWayland` runs as an ordinary client of the sway session that is already in the tree**, using `xdg_toplevel` + client-side decorations, and that is sufficient for everything through G4. The Wayland objections that matter â€” no absolute positioning, no client z-order, no global event taps, no active-window capture, no surface embedding â€” are all *guest* objections, and every one of them evaporates at G5 when `owsd` and the compositor are the same process on wlroots (MIT, C, in tree). Building `OWSBackendX11` first would mean encoding ICCCM/EWMH semantics into the window model and then rewriting it. X11 stays supported because Xorg 21.1.24 and xfwm4 already build and give us a legacy-app host and a no-GPU escape hatch; it does not receive new semantics.

---

## 3. DECISION: fork-and-modernize AppKit, do not write fresh

**Fork.** AppKit's public surface is roughly 4,000 exported symbols. The clean-room constraint does not require us to author every line; it requires that what we ship not derive from Apple source. Cocotron was written from public Cocoa documentation and headers, is MIT, and therefore satisfies the constraint *and* inherits a clean provenance chain rather than starting a new one.

The alternatives were evaluated and rejected:

| Option | License into APSL/BSD image | Runtime | Verdict |
|---|---|---|---|
| **darling-cocotron** | **MIT** â€” attribution only | works on real objc4 once its bundled runtime is deleted | **Chosen** |
| GNUstep `libs-gui` | LGPL-2.1; large patches need FSF assignment | wants libobjc2, which is structurally incompatible with objc4 | Rejected |
| Darling proper | GPL-3.0 | â€” | **Study only. Never vendor.** |
| From scratch | n/a | n/a | Rejected â€” a decade, for no legal gain |

Three hard rules fall out. **(a) Nothing that links into the shippable image may depend on libobjc2** â€” real `.app` binaries are built `-fobjc-runtime=macosx` and objc4's `objc_class`/`class_ro_t`/`class_rw_t` split is not compatible with the GNUstep runtime. **(b) Per-file license audit before any merge**: Darling's own additions inside an MIT tree may be GPL-3; record every file's origin and upstream commit hash in `THIRD_PARTY_LICENSES.md`. **(c) The one place we write fresh is the backend** â€” Cocotron's Win32-first, "needs a lot of work" X11 backend is replaced wholesale by `libOWSClient`, and backends live **under CoreGraphics, not inside AppKit**, so no platform assumption ever reaches view code.

Do not convert Cocotron to ARC; keep manual retain/release and let client apps use ARC, which objc4 provides for free.

**The unadvertised critical path is Foundation, not AppKit.** `src/Libraries/Foundation` on `openosx-next` is six MPL-2.0 stub classes â€” no `NSBundle`, no `NSKeyedUnarchiver`, no `NSRunLoop`, no `NSNotificationCenter`, no `NSTimer`. Nib loading *is* `NSBundle` + `NSKeyedUnarchiver`. Cocotron ships a complete Foundation; toll-free bridging it onto CF 1338 (extending the existing `NSCFString.m` pattern) is the single largest task in this plan and it is unavoidable, because real apps pass `NSString *` into `CFStringGetCString()` constantly.

---

## 4. WindowServer design (`owsd`)

**Bootstrap.** One daemon per login session, registered with launchd as the Mach service `org.openosx.windowserver`. Clients reach it with `xpc_connection_create_mach_service`. Connections are integer-keyed (`OWSConnectionID`) and server-stateful: every window, surface and event route hangs off one, and connection teardown garbage-collects all of them.

**Transport, split by traffic shape.** Control plane over **XPC**: free launchd bootstrap, on-demand launch, peer credentials, typed dictionaries, and Mach send rights and shared-memory regions as first-class payload types. We deliberately do **not** hand-roll MIG â€” MIG's rigidity is a large part of why CGS is unversionable, and typed XPC deserialization removes the bug class responsible for most of WindowServer's CVE history. Event plane is a **lock-free SPSC ring in shared memory with a Mach-port doorbell**; the client drains it from a `CFMachPort` run-loop source, which is exactly how `NSApplication` already expects to receive events. Pixels never appear in a message.

**Surface sharing.** PDSurface is already an IOSurface-shaped API â€” `PDSurfaceCreate`, `PDSurfaceGetID`/`PDSurfaceLookup`, `PDSurfaceFlush(rect)`, `PDSurfaceSetScanout` â€” and its own header says the opaque ID "is what stands in for a dmabuf file descriptor". The work is hardening, in this order:

1. **`PDSurfaceCreateMachPort` / `PDSurfaceLookupFromMachPort`.** A guessable integer ID is ambient authority in a server that handles other users' pixels. A send right is an unforgeable, revocable, XPC-transferable capability, and it makes surface lifetime track port lifetime.
2. **Seed counter and `Lock`/`Unlock`** â€” required for correctness the moment the GPU and CPU touch a surface concurrently.
3. **`PDSurfaceCreateXPCObject`** â€” trivial after (1); keeps the control protocol clean.
4. **Pin `kPDSurfaceFormat*` to DRM FourCCs** so the gbm/wlroots mapping is an enum cast, not a translation table.
5. Planar/YUV â€” deferred to the video phase.

Because we own `libgbm`, `libdrm`, PDSurface *and* the compositor, the destination path is **app â†’ PDSurface â†’ owsd â†’ scanout, zero copies, no Wayland involvement**. Until G5 we use `wl_shm` instead, which costs a blit (~8 MB/frame at 1080p). That is ugly, expected, and acceptable; it removes an entire class of blocking work from the path to first pixels. Do not relitigate it monthly.

**Event delivery.** Adopt Darling's invariant verbatim: *everything is an `OWSEventRecord` before it is a `CGEventRef`, and a `CGEventRef` before it is an `NSEvent`.* If events are born as `NSEvent`, then `CGEventTap`, `CGEventCreateMouseEvent`, `+[NSEvent addGlobalMonitorForEvents:]` and the Carbon `EventRef` paths are all dead ends. Synthetic per-connection injection and physical seat input are separate entry points carrying different provenance flags.

**Three protocol departures from Apple, cheap now and expensive later.** Explicit damage rectangles plus a surface seed on every commit (Wayland demands damage anyway); an XPC-entitlement capability model â€” "may enumerate other connections' windows", "may inject events", "may capture screen" â€” instead of `CGSSetUniversalOwner`; and async-by-default setters with an explicit `OWSSync()` barrier, rather than a synchronous round trip per window move. Version the protocol from the first commit: `OWSProtocolVersion` negotiated at connect, server supports N and Nâˆ’1.

### The CGS trade-off, stated explicitly

We are **not** replicating Apple's private CGS byte-for-byte, and attempting it would be this project's worst strategic error. But the usual framing â€” "we own both ends, so we need no compatibility" â€” undercounts. Split the app-facing surface into three tiers:

- **T1 â€” AppKit/CoreAnimation/CoreGraphics drawing.** ~100% of apps. Served by **our own protocol**, since we write both ends.
- **T2 â€” *public* CG window/display/event APIs**: `CGDisplayBounds`, `CGGetActiveDisplayList`, `CGWindowListCopyWindowInfo`, `CGDisplayCreateImage`, `CGEventTapCreate`, `CGWarpMouseCursorPosition`. These are documented, stable, and called by ordinary well-behaved apps doing screenshots, hotkeys and multi-monitor layout. **We must serve these faithfully.** They are a few dozen functions with published semantics and they unlock apps like nothing else does.
- **T3 â€” private `CGSâ€¦`/`SLSâ€¦` calls.** Window managers, Dock replacements, a scattering of blur tricks. **Best-effort shim behind a feature flag; `kCGErrorFailure` for the rest, and no fidelity promise ever.** T3 is undocumented, version-skewed, and unbounded. Note the escape hatch: most T3 callers want a *capability* (list windows, register a hotkey, capture the screen), so exposing those as first-class OpenOSX APIs under the capability model drains most of the pressure â€” with a sane security model instead of Apple's.

Reverse-engineered CGS header collections (e.g. `NUIKit/CGSInternal`, MIT) are legitimate published RE and may be used as a *specification of names*. They are not a spec we owe fidelity to.

---

## 5. Aqua presentation

**Window decorations are client-side, drawn by AppKit. This is not a compromise â€” it is what macOS does:** `NSThemeFrame` is client-side and the WindowServer composites rather than draws. AppKit requests undecorated frames (`xdg-decoration` mode `client`; `_MOTIF_WM_HINTS` on the X11 fallback) and initiates interactive move/resize via `xdg_toplevel.move` / `_NET_WM_MOVERESIZE`.

We will **not** express Aqua chrome through xfwm4's `themerc` engine. It is a raster pixmap format with no per-window variation, a fixed button vocabulary, no procedural drawing, and per-button-only prelight â€” so unified titlebars, `NSWindowStyleMaskFullSizeContentView`, toolbar merging, sheets, vibrancy and group traffic-light hover are all inexpressible in it. Worse, chrome drawn there would live in GPLv2 code. CSD gives unlimited fidelity, keeps the chrome inside APSL/BSD AppKit, is semantically correct (`styleMask` and `NSToolbar` are *client* concepts), and renders identically in hosted and native modes â€” so G2 chrome work survives G5 intact.

Foreign GTK/Qt/X11 apps still get server-side decorations from a first-party theme (`button_layout=CHM|`, alpha corner pixmaps, prelight glyphs) so they look like they belong. That is a design-asset task, not an engineering one.

**Global menubar: its own process, three lanes.** Not a panel plugin â€” it must exist independently of the panel, own global key-equivalent grabs, render with our own text stack, and survive the move to Wayland (where GtkPlug/GtkSocket panel embedding does not exist at all).

1. **Native lane (primary).** Our own menu protocol over XPC. `NSMenu` carries state DBusMenu cannot represent: `NSAttributedString` titles, `NSMenuItem.view` custom views, full `NSEventModifierFlags` key equivalents, lazy `NSMenuDelegate` population, and `validateMenuItem:` walking the responder chain *at menu-open time*. The menubar asks the frontmost app lazily; the app evaluates delegates and validation in its own address space and returns a render-ready tree.
2. **Export lane.** Publish the same menus as `com.canonical.dbusmenu`. Cheap, buys interop and debuggability.
3. **Import lane.** Implement `com.canonical.AppMenu.Registrar`, read `_GTK_MENUBAR_OBJECT_PATH`, and set `Gtk/ShellShowsMenubar=1`, so cross-built GTK and Qt apps land in the same bar. Also host `org.kde.StatusNotifierWatcher` so foreign tray icons join `NSStatusItem` extras in one strip. **This is what makes it read as one OS.**

Activation is authoritative in `owsd`, never derived from window-manager focus: macOS activates *applications*, an app stays active with zero windows, and `key` and `main` window state are two orthogonal bits that the chrome must reflect independently.

**The look is ours.** Ship zero Apple assets â€” no `.icns`, cursors, wallpapers, sounds or asset catalogs, enforced by a CI check on the image, not by memory. Ship no Apple fonts: San Francisco, Lucida Grande and Helvetica Neue are all licensed away from us. Use **Inter** (OFL) as the system font resolution for `+[NSFont systemFontOfSize:]`, JetBrains Mono or IBM Plex Mono for fixed-width, and ship a fontconfig alias table (`Helvetica`/`Helvetica Neue` â†’ TeX Gyre Heros, `Menlo`/`Monaco` â†’ JetBrains Mono) â€” without it a large fraction of real apps render as fallback boxes for reasons unrelated to AppKit. Aqua, Cocoa, Quartz, Finder and Spotlight are registered Apple trademarks and must not name user-visible OpenOSX components; API identifiers and framework install names may match exactly, because those are ABI and a binary will not link without them. Name the design language separately from the DE and document it in an `OpenOSX Human Interface Guidelines` with real numbers â€” 4pt base unit, 22/28/32pt control heights, 6pt control / 10pt window radii, 28pt titlebar, 24pt menubar â€” and a semantic colour token set whose names *are* the `NSColor` semantic names (`labelColor`, `controlAccentColor`, `windowBackgroundColor`, â€¦), consumed by AppKit, the GTK theme and the WM theme alike so light/dark and accent switch atomically across toolkits. Pick a few deliberate divergences so the result is a sibling of Aqua and not a knockoff: our own traffic-light glyph set and colour ramp, a menubar that never auto-hides, and first-class named restorable Sessions.

---

## 6. The forked-XFCE identity plan

Do not fork XFCE uniformly. Split by what each component *is*.

| Component | License | Disposition |
|---|---|---|
| **xfwm4** | GPLv2 | **Hard fork** (X11 fallback path only). Upstream is X11-only forever â€” Xfce's Wayland answer is xfwl4, a from-scratch Rust/Smithay project â€” so divergence now costs nothing in future merge pain. Changes: suppress compositor shadows for CSD windows, honour `NSWindowLevel` via a private atom, release Super/Command to apps, route Cmd-Tab to our app switcher. |
| **xfce4-panel** | GPLv2 / LGPLv2.1 | **Fork (light)**, transitional only. The menubar is a new native process, not a themed panel; the panel is retired once the menubar and a Shelf exist. |
| **xfdesktop** | GPLv2 | **Fork (light).** Small, stable, cheap to carry. |
| **xfce4-session** | GPLv2 | **Retire â†’ launchd.** Already in tree, already better at ordering. Deletes the `xfce4-session-sway-compositor.patch` too. |
| **xfconfd** | GPLv2 | **Retire â†’ `openosxsettingsd`.** See below. |
| **garcon** | LGPLv2 | **Retire.** `.desktop` menus are the wrong model for a bundle OS; replace with a LaunchServices-style database over `/Applications/*.app` plus a `.desktop` importer. |
| **thunar, exo, libxfce4util/ui, libxfce4windowing, tumbler** | GPL/LGPL | **Track upstream** with a rebrand-only patch queue. Read `libxfce4windowing` as a model for the backend split even where we do not link it. |
| **xfce4-settings** | GPLv2 | **Fork `xfsettingsd`** (it publishes the XSettings that gate `Gtk/ShellShowsMenubar`); replace the dialogs with native panes. |
| **xfce4-terminal, xfce4-appfinder** | GPLv2 | Track, then replace with native AppKit apps. |

**The highest-leverage single change is `openosxsettingsd`:** a native APSL/BSD daemon implementing the published `org.xfce.Xfconf` D-Bus interface and XSettings on top of CFPreferences, replacing `xfconfd` entirely. Every XFCE component depends on xfconf, so this one substitution removes GPL code from the deepest plumbing layer, is clean-room by construction (a published wire protocol with introspection XML), is a few thousand lines, and unifies the settings story: `defaults write com.openosx.WindowManager â€¦` changes desktop behaviour. That is an OS, not a respin. **Identity comes from replacing the plumbing, not from theming the pixels** â€” and the global menubar is the single most recognisable structural difference from every other Linux desktop.

**License architecture â€” one hard rule.** APSL 2.0 is GPL-incompatible. Shipping GPL and APSL code in one *image* is mere aggregation and is fine; **combining them in one linked work is not.** Therefore no OpenOSX-authored framework may ever link or vendor GPL code, and the only thing crossing the boundary to xfwm4/panel/thunar is a wire protocol (Wayland, X11, D-Bus). Conveniently this is also the right architecture. GPLv2 rebranding is permitted â€” rename binaries, `.desktop` files, icon themes and `org.xfce.*` bus names (keeping aliases so third-party plugins still work), mark changed files as modified per Â§2(a), keep `COPYING`, and publish per-release source tarballs. Avoid the Xfce name and logo in branding. LGPL components (libxfce4util/ui) must keep standalone dylibs on disk beside any future dyld shared cache, or the relink obligation is unsatisfiable. Vendor each forked component via `git subtree`, matching the existing `tools/kc-tools` and `tools/xnu-loader` pattern.

---

## 7. Phase plan

Each phase is independently demonstrable and has an exit criterion assertable in CI.

**G0 â€” Instrumentation and scaffolding.** Build the `.tbd` parser, the corpus scanner (`dyld_info -bind` over real `.app` bundles), and the coverage differ producing `missing_hard` / `missing_spec` / `extra`; generate **abort-stub dylibs** for AppKit, Foundation, CoreGraphics, CoreText and QuartzCore with correct install names, compatibility versions, re-export edges and bundle layout â€” including data symbols, which are dyld hard-fails and which naive generators miss. Harden PDSurface (Â§4). Scaffold `owsd` with `OWSBackendHeadless` and a pixel-hash harness. Land `CLEANROOM_POLICY.md`, the DCO-plus attestation, and the provenance-header CI check.
*Exit:* a real unmodified `.app` gets past dyld and prints an ordered `[CG-STUB]` call trace before aborting; headless CI asserts a window tree and surface hash with no GPU. **This alone is worth announcing** â€” it is exactly the harness Darling never built, and it converts the rest of this plan from argument into a ranked work queue.

**G1 â€” A window with correct pixels.** CG geometry and affine transforms; CFRuntime registration for every CG type so `CFRetain(cgimage)` genuinely works (this is where we beat Cocotron, which had no real CF); `CGBitmapContext` + `CGContext` core on `O2Context_cairo`; the base-CTM y-flip nailed by unit test on day one. `OWSBackendWayland`: one `OWSWindow` â†’ one `xdg_toplevel`, PDSurface â†’ `wl_shm` â†’ `wl_buffer`, damage â†’ `wl_surface.damage_buffer`.
*Exit:* a raw C client draws paths, gradients and images with public CG calls into a window inside the existing sway session, pixel-diffed against golden images in CI.

**G2 â€” AppKit renders (static).** Foundation on real CF: delete Cocotron's runtime and CoreFoundation, toll-free-bridge the collection and string classes, audit every method signature against the 64-bit ABI (`NSInteger`/`CGFloat`, and `objc_msgSend_stret` for `NSRect` returns â€” get this wrong and you get garbage rects with no crash), add block-taking APIs. `NSApplication`/`NSWindow`/`NSView`/`NSCell` up through `NSButton` and `NSTextField`. CSD chrome via our `NSThemeFrame` equivalent. Text through FreeType + HarfBuzz behind CoreText.
*Exit:* an `.app` bundle whose UI is constructed in code renders correctly, with Aqua-shaped chrome and correct text metrics. Not yet clickable.

**G3 â€” Interactive.** Input router in `owsd`: seats, hit-testing against window shape respecting level and order, cursor. `wl_pointer`/`wl_keyboard` â†’ `OWSEventRecord` â†’ `CGEventRef` â†’ `NSEvent`, drained by a `CFMachPort` run-loop source into `sendEvent:` and the responder chain. xkb keysyms â†’ Apple virtual keycodes; Superâ†’Command with a documented reserved-chord list. `key` and `main` as separate bits.
*Exit:* clicking a button fires its action, typing into an `NSTextField` produces characters, dragging the titlebar moves the window, Cmd-Q quits.

**G4 â€” A real third-party app opens.** `NSKeyedUnarchiver` for classic keyed-archive nibs plus `NSNibArchiveUnarchiver` written from the published NIBArchive spec (Xcode 13+ compiles macOS XIBs with `UINibEncoder`, so both formats are mandatory), magic-sniffing dispatch in `NSNib`, differential-tested against the open Python/Rust parsers. Then burn down the class list the decoder logs â€” a nib names every class, sets every property and wires every outlet, so **nib decoding is the conformance test that prioritises all remaining AppKit work.** Menubar process with all three lanes; app-level activation.
*Exit:* a small, unsigned, AppKit-only third-party `.app` â€” no Metal, no AVFoundation, no Swift â€” opens its main window with a working global menubar.

**G5 â€” Usable, native, and identity-complete.** `owsd` absorbs the compositor role on wlroots and sway is retired; a `linux-dmabuf`-shaped OpenOSX protocol carries PDSurface Mach ports, making the zero-copy path live, with `PDSurfaceSetScanout` as the fullscreen fast path. Server-side shadows, rounded corners, blur, `NSWindowLevel` enforcement, sheets anchored to parent titlebars, ExposÃ©. The T2 public CG surface served under the capability model. `openosxsettingsd`, launchd session, XFCE plumbing retired. `OWSLayerTree` for server-side CoreAnimation and `CALayerHost`-equivalent cross-process embedding. `libCGSCompat` as a flagged, unsupported T3 shim.
*Exit:* a real third-party application is **usable** â€” scrolling, text editing, menus, dialogs, copy/paste, multi-window â€” a third-party screenshot utility and a third-party global-hotkey utility work unmodified, and GTK/Xwayland apps remain first-class in the same session.

---

## 8. Effort and risk, honestly

**Rough calendar at sustained hobby cadence, one to three people:** G0 one to two months. G1 three to four. G2 **six to nine â€” the long pole, and it is Foundation, not AppKit.** G3 four to six. G4 four to six. G5 six to twelve. Call it **two and a half to four years to "a real third-party app is usable"**, with wide error bars. Front-loading is intentional: G0 is the cheapest phase and the one that determines whether the rest is planned or guessed.

**Where projects like this die.** Text is first â€” GNUstep's Opal still lists its CoreText layer as the largest remaining gap after fifteen years. Do not write shaping or linebreaking; use HarfBuzz and libunibreak, both already in the image via Pango, and implement FreeType subpixel positioning from the start because retrofitting it regresses every text layout at once. Second is symbol *coverage*, which is a burndown, not a design problem â€” one missing data symbol kills a process before `main()`. Third is the temptation to chase T3 CGS fidelity; refuse it.

**Calibration.** Darling has worked on this for over a decade with a competent team and still reports that GUI apps beyond hello-world generally fail. Two asymmetries are genuinely in our favour and are the reason this is tractable at all: most of Darling's cost is `darlingserver`, Mach-on-Linux, the Mach-O loader and Darwin syscall shims â€” **we pay none of it, because we run real XNU, real Mach ports, real dyld and real launchd** â€” and Darling never built the compositor, which we already have. Their trajectory (GNUstep gui â†’ Qt â†’ Cocotron) is a paid-for experimental result; we take the answer without repeating the experiment.

**What may never be feasible.** **Metal** â€” a multi-year project on its own requiring Vulkan translation plus an AIR bytecode shader compiler; note it and move on, it is not on the critical path to anything in G0â€“G5. **DRM'd and FairPlay-protected apps** â€” assume never. **App Store apps requiring receipt validation or Apple's code-signing trust roots** â€” we cannot mint Apple trust, so assume never. **arm64-only applications** â€” would need a Rosetta-equivalent, i.e. a separate project of comparable size to this one. **Swift ABI-stable binaries** linking `/usr/lib/swift/*` need the full stdlib and runtime ported; that is Apache-2.0 and therefore a large *porting* job rather than a reimplementation job, but it is nowhere near G0â€“G5. Also unbudgeted above and each a real subsystem: **input methods / IME** (TSM and InputMethodKit on macOS, `text-input-v3` on Wayland â€” CJK users hit this immediately), **pasteboard and drag-and-drop** as a separate XPC service bridging `wl_data_device`, **CoreAudio and AVFoundation**, and **accessibility**.

**Two operational risks worth naming now.** Server-side surface leaks are a known IOSurface-shaped failure mode; build connection-scoped reclamation and an `owsctl surfaces` introspection command from day one. And `owsd` is a privilege boundary reachable from every sandbox â€” using typed XPC rather than hand-rolled MIG is a genuine architectural improvement over Apple's design, not merely a convenience, and should be stated as such.

---

## 9. Clean-room, non-negotiable

The perimeter is narrow and that is the good news: XNU, libSystem, dyld, objc4, CoreFoundation, Security, IOKit and launchd are **published by Apple under APSL/BSD** â€” that is licensed use, not reverse engineering. Clean-room discipline applies only to the never-published frameworks: **AppKit, CoreGraphics/Quartz, CoreAnimation, WindowServer/CGS, Metal, AVFoundation, CoreAudio.**

Permitted, always: Apple's public developer documentation and public SDK headers (as a source of *facts* â€” re-declare, never paste, and never vendor Xcode SDK headers into the repo); `.tbd` interface stubs; published reverse-engineering write-ups; observed ABI and runtime behaviour of binaries we lawfully possess; and compatibly-licensed open reimplementations. Forbidden absolutely, and contribution-disqualifying for this subtree: **any non-public Apple source, however obtained.** Rewriting cures copyright derivation; it does **not** cure trade-secret taint, and a contributor who has read leaked source hands a plaintiff the "access" element for free â€” which is fatal precisely because a compatible implementation is *made of* ABI-forced similarity. Unlike ReactOS, we draw that line in advance and in writing: such contributors remain welcome everywhere else in the project.

Interface facts may match Apple exactly â€” exported symbol names, selectors, struct layouts, enum values, framework install names â€” because a binary will not link otherwise. Expression may not, and the specific fingerprint that convicted ReactOS in outside eyes was *internal* names: unexported functions, private struct members, macro conventions. Every file in the clean-room subtree carries a provenance header naming its tier and sources, enforced by CI, alongside a CI grep for Apple SDK header fingerprints outside APSL-sourced directories and an image-level check for Apple-origin artwork, fonts and sounds. Keep the commit history unsquashed: a visible trail of wrong guesses and fixed bugs is affirmative evidence of independent creation.

*This document is engineering analysis, not legal advice. The rasterizer license election (Cairo under MPL-1.1) and the project name are the two items worth a paid hour with an IP attorney before a public 1.0.*
