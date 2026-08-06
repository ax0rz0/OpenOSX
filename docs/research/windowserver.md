# OpenOSX WindowServer â€” Display-Server Architecture & Aqua Translation

**Scope:** how macOS actually gets pixels on screen, what of that is legally reproducible, and a concrete design for a native OpenOSX display server that hosts our own AppKit and translates onto the wlroots/XFCE stack the project already ships.

---

## 0. Executive summary, and two corrections to the brief

**Three findings that change the plan before it starts:**

1. **The compositor in `openosx-next` is sway (wlroots), not xfwm4.** I checked the tree: `nix/pkgs/wayland/{wlroots,sway,gtk-layer-shell}.nix` exist, `nix/pkgs/xfce/xfce4-session-sway-compositor.patch` wires sway in as the session compositor, and `nix/pkgs/xfce/sway-config` explicitly says *"xfce4-panel and xfdesktop take those roles through the layer-shell protocol."* There is **no xfwm4 package**. The DE-fork target for Aqua chrome is therefore **sway/wlroots + xfce4-panel**, not xfwm4. This is *better* news â€” a wlroots compositor is a library you build a compositor out of, so "fork" means "write ~3k lines of our own compositor against wlroots," not "patch a 20-year-old X11 WM."

2. **OpenOSX already has an IOSurface-shaped API and it already knows it.** `src/Libraries/PDSurface/include/PDSurface.h` is a driver-independent shareable-buffer API with `PDSurfaceCreate`, `PDSurfaceGetID`/`PDSurfaceLookup`, `PDSurfaceFlush(x,y,w,h)`, `PDSurfaceSetScanout`. Its own comment reads: *"This is what stands in for a dmabuf file descriptor."* The IOSurface question in the brief is 80% already answered â€” the work is **hardening and Mach-porting** PDSurface, not inventing it.

3. **The "do we need Apple's CGS protocol?" trade-off resolves cleanly â€” but not the way the brief frames it.** The brief says we only need CGS "for apps that bypass AppKit." That undercounts. A large set of *public, documented* CoreGraphics APIs â€” `CGDisplayBounds`, `CGWindowListCopyWindowInfo`, `CGEventTapCreate`, `CGWarpMouseCursorPosition`, `CGDisplayCreateImage` â€” are **thin client stubs over the same WindowServer connection**. Ordinary, well-behaved AppKit apps call them constantly (screenshot tools, hotkey handlers, multi-monitor logic). So: we don't need Apple's *private wire format*, but we absolutely need the *public CG semantics* served by our display server. Design accordingly.

**Recommended shape:** a single daemon `owsd` (OpenOSX WindowServer) that owns (a) our own versioned control protocol over XPC, (b) a shared-memory + Mach-port event ring, (c) PDSurface-backed window backing stores, and (d) a pluggable *presentation backend* â€” first a **Wayland-client backend** (rootless, one NSWindow â‰ˆ one `xdg_toplevel`, runs under today's sway), later a **native wlroots-compositor backend** where `owsd` *is* the compositor and Aqua fidelity becomes total.

---

## 1. How macOS apps actually get pixels on screen

Everything below is sourced from Apple's public documentation, published security/RE write-ups, and open-source reimplementations. **No leaked Apple source is referenced, and none should be consulted.** I have flagged the provenance tier of each claim in Â§1.6.

### 1.1 The connection: bootstrap, Mach, MIG

An app does not draw to the framebuffer. It draws into memory it shares with a privileged daemon.

- `WindowServer` lives at `/System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer` â€” historically it was inside CoreGraphics; Apple moved the implementation into **SkyLight** (~10.12), leaving CoreGraphics as a compatibility faÃ§ade. ([Keen Lab](https://keenlab.tencent.com/en/2016/07/22/WindowServer-The-privilege-chameleon-on-macOS-Part-1/), [Boutnaru](https://medium.com/@boutnaru/the-macos-process-journey-windowserver-file-system-events-daemon-3b619f276fd1))
- It registers a Mach service with launchd under the global name **`com.apple.windowserver.active`**. Clients â€” including sandboxed ones, whose sandbox profiles carry an explicit `(allow mach-lookup (global-name "com.apple.windowserver.active"))` â€” obtain a send right via `bootstrap_look_up`. ([Keen Lab](https://keenlab.tencent.com/en/2016/07/22/WindowServer-The-privilege-chameleon-on-macOS-Part-1/), [HackTricks](https://hacktricks.wiki/en/macos-hardening/macos-security-and-privilege-escalation/macos-proces-abuse/macos-ipc-inter-process-communication/index.html))
- The transport is **MIG over `mach_msg`**. Keen Lab's write-up describes it precisely: client-side stubs prefixed `CGS`/`CAS` "obtain the target mach port, compose a mach message and send the message by calling `mach_msg`"; server-side handlers are named `__Xâ€¦` and are dispatched by message ID in per-framework server loops. This is a hand-rolled MIG-style subsystem, not a self-describing protocol.
- The handle a client holds is a **`CGSConnectionID`** â€” an integer session token, not a port. `CGSMainConnectionID()` returns the process's default one; `CGSNewConnection`/`CGSReleaseConnection` manage additional ones. Apple exposes exactly one crumb publicly: `CGWindowServerCFMachPort()`. ([Apple](https://developer.apple.com/documentation/coregraphics/cgwindowservercfmachport()), [NUIKit/CGSInternal](https://github.com/NUIKit/CGSInternal/blob/master/CGSConnection.h))
- WindowServer is also the **session authority**: `_XCreateSession` forks and launches `loginwindow` at the user's privilege level. Login session ownership is a WindowServer concept, not a launchd one. ([Keen Lab](https://keenlab.tencent.com/en/2016/07/22/WindowServer-The-privilege-chameleon-on-macOS-Part-1/))

**Architectural consequence for us:** the connection is *per-process, integer-keyed, and stateful on the server*. Every window, surface, and event route hangs off it, and teardown of a connection must garbage-collect all of them. Reproduce that ownership model even though we won't reproduce the wire format.

### 1.2 The CGS/SkyLight API surface

From the RE'd headers and consumer projects (yabai, Hammerspoon, AltTab, Cua) the surface decomposes into stable families. Naming migrated `CGSâ€¦` â†’ `SLSâ€¦` (SkyLight Server) with `CGS` kept as aliases:

| Family | Representative names | Purpose |
|---|---|---|
| Connection lifecycle | `CGSMainConnectionID`, `CGSNewConnection`, `CGSReleaseConnection`, `CGSGetConnectionIDForPSN` | session handle mgmt |
| Connection properties | `CGSCopyConnectionProperty`, `CGSSetConnectionProperty` | arbitrary K/V bag shared through the server |
| Update batching | `CGSDisableUpdate` / `CGSReenableUpdate` | atomic multi-window transactions, with a server-side watchdog timeout |
| Notifications | connection created/destroyed, window created/moved/ordered | serverâ†’client push callbacks |
| Windows | `SLSNewWindow`, `SLSNewWindowWithOpaqueShape`, order/level/shape/alpha/shadow setters | the actual window objects |
| Surfaces | window backing-store binding | pixels |
| Spaces | space enumeration, windowâ†”space assignment | Mission Control |
| Events | `CGSGetEventPort`, `SLPSPostEventRecordTo`, `SLEventPostToPid`, `SLPSSetFrontProcessWithOptions` | input routing and synthesis |
| Privileged | `CGSSetUniversalOwner`, `CGSSetLoginwindowConnection` | Dock/loginwindow escalation |

Two behavioural details from the Cua write-up are worth internalising because they reveal the *semantics*, not just the names ([Cua](https://cua.ai/blog/inside-macos-window-internals)):

- **Activation is two separable operations.** `SLPSPostEventRecordTo` flips a window's AppKit-internal *active* state; `SLPSSetFrontProcessWithOptions` performs the *visible* raise and Space switch. macOS conflates them in normal use, but they are distinct server calls. Our protocol should keep them distinct from day one â€” it's what makes background/offscreen automation and multi-seat possible.
- **Per-PID event injection is a first-class path**, distinct from the HID stream (`SLEventPostToPid` bypasses `IOHIDPostEvent`). Apps *can* detect the difference. Design implication: model "synthetic event to a connection" and "physical event from a seat" as different entry points with different provenance flags.

### 1.3 Backing stores, surfaces, and IOSurface

**IOSurface is a public framework** â€” `IOSurfaceCreate`, dimension/stride/pixel-format/plane properties, `IOSurfaceLock`/`IOSurfaceUnlock`, a **seed** counter for change detection, and cross-process handles via `IOSurfaceCreateMachPort` / `IOSurfaceLookupFromMachPort` / `IOSurfaceCreateXPCObject`. ([Apple](https://developer.apple.com/documentation/iosurface))

Conceptually it is *"a high-level abstraction around a chunk of system shared memoryâ€¦ a kernel-managed chunk of texture memory that can be paged on or off the GPU automatically and shared across processes"* ([Russ Bishop](http://www.russbishop.net/cross-process-rendering)). When an app updates its UI, **no pixels are copied** to the compositor â€” the surface is already shared; only metadata moves. WindowServer then owns positioning, ordering, clipping, masks, shadows and effects. ([Fortuna](https://andreafortuna.org/2025/10/05/macos-windowserver/))

The failure mode is instructive and we will inherit it: leaked surfaces accumulate in the server. Real bugs exist where IOSurface buffers grow to multi-GB because clients never release. ([cmux#1435](https://github.com/manaflow-ai/cmux/issues/1435)) Budget for server-side accounting and connection-scoped reclamation from the start.

### 1.4 CoreAnimation's role â€” the second server

CoreAnimation is not a drawing library bolted onto AppKit; it is **a client/server split of its own** ([Vaidyam](https://avaidyam.github.io/2018/02/22/SecretLife_CoreAnimation.html), [Arbuckle](https://github.com/EthanArbuckle/ios-rendering-docs)):

- A **`CAContext`** embodies an independent rendering surface + layer tree registered with the render server, identified by a global **`CAContextID`**.
- A **`CALayerHost`** is a `CALayer` subclass that *displays another process's `CAContext`* by ID. This is the mechanism behind out-of-process views (WebKit content, plugin surfaces) and is the closest macOS analogue to Wayland subsurfaces. ([TeamDev](https://medium.com/teamdev-engineering/cross-process-rendering-using-calayer-885dd2a94c1e))
- The app commits a **layer tree description** (geometry, transforms, animation curves, backing surfaces), not a pixel stream. The render server rasterises/composites and drives the GPU. Because animations are *declarative* and live server-side, a wedged app keeps animating.

On macOS the render server is inside WindowServer; on iOS it lives in `backboardd`. **We get to choose.** Recommendation in Â§2.

### 1.5 The event path: HID â†’ WindowServer â†’ app run loop

```
HID device â†’ IOKit HID (IOHIDSystem/IOHIDFamily)
  â†’ WindowServer: hit-test, focus, Space, cursor, tap dispatch
    â†’ per-connection Mach port (CGSGetEventPort)
      â†’ CGSEventRecord  â†’ CGEventRef  [â†’ Carbon EventRef]
        â†’ NSEvent via CFRunLoop source on the app's main thread
          â†’ NSApp.sendEvent: â†’ NSWindow.sendEvent: â†’ NSResponder chain
```

- The app *"receives these events through an input source installed in the main thread's run loop"* â€” a `CFMachPort` run-loop source, the same primitive `CGEventTap` consumers use via `CFMachPortCreateRunLoopSource`. ([Apple Cocoa Event Handling Guide](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/EventArchitecture/EventArchitecture.html))
- `NSApp` pulls the event, converts, and dispatches: *"NSApp merely forwards the event to the window in which the user action occurred by invoking the `sendEvent:` method of that NSWindow object,"* which routes to the `NSView` via `mouseDown:`/`keyDown:`. ([Apple](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/EventArchitecture/EventArchitecture.html))
- **Darling's own backend-rework design states the invariant we should adopt verbatim:** *"all events must exist as `CGSEventRecord`s prior to becoming `CGEvent`s"* and then `NSEvent`s, with backend events posted to the Mach port from `CGSGetEventPort()`, and the X11 backend consuming XInput2. ([darling#937](https://github.com/darlinghq/darling/issues/937))

That last point is the single most valuable piece of prior art in this whole report: an existing open-source project has already worked out the layering for exactly our problem, and its conclusion is *"put the backend below CoreGraphics, not below AppKit."*

### 1.6 Provenance tiers â€” what we may rely on

| Tier | Content | Use |
|---|---|---|
| **A â€” Public Apple docs & SDK headers** | IOSurface API, Cocoa Event Handling Guide, HIG, `CGWindowServerCFMachPort`, all public CG/AppKit/CA headers | âœ… Implement directly against these |
| **B â€” Published RE / observable behaviour** | SkyLight function taxonomy, `com.apple.windowserver.active`, MIG dispatch shape, Keen Lab & Cua write-ups, `NUIKit/CGSInternal` | âœ… Use as *behavioural specification*. Cite the write-up, not a binary |
| **C â€” Compatible open-source reimplementations** | Cocotron (MIT), GNUstep `GSDisplayServer` (LGPL), wlroots (MIT), Cairo (LGPL/MPL) | âœ… Study; vendor per license (Â§6) |
| **D â€” Prohibited** | Any leaked Apple source, any decompiled listing derived from it, Darling's GPL-3 code as a *source* to copy | âŒ Never. A contributor who reads it is permanently tainted for these components |

**Process control to institute now:** a `CONTRIBUTING-CLEANROOM.md` for `src/Frameworks/**` and `src/Servers/owsd/**` requiring every non-obvious semantic to carry a comment citing its tier-A/B/C source, plus a contributor attestation. This is the ReactOS lesson: the taint is retroactive and unrecoverable.

> **Darling caveat:** Darling is GPL-3. Reading its *issue tracker and design discussions* is fine (that's public technical writing, tier B/C). **Do not vendor Darling code, and do not have the same person who reads Darling's AppKit source write ours** if we want a BSD/APSL-clean result. Darling's *Cocotron fork* is MIT-derived and is the piece to actually reuse.

---

## 2. Designing "OpenOSX WindowServer" (`owsd`)

### 2.1 The central trade-off, assessed explicitly

**Question:** since we ship our own AppKit, do we need to replicate Apple's private CGS wire protocol?

**Answer: No â€” and attempting it would be the project's worst strategic error.** But the framing needs correcting.

Split the app-facing surface into three tiers:

| Tier | What | Who calls it | Must we serve it? |
|---|---|---|---|
| **T1** | AppKit / CoreAnimation / CoreGraphics *drawing* | ~100% of apps | **Yes** â€” but through *our own* protocol, since we own both ends |
| **T2** | **Public** CG window/display/event APIs: `CGDisplayBounds`, `CGGetActiveDisplayList`, `CGWindowListCopyWindowInfo`, `CGDisplayCreateImage`, `CGEventTapCreate`, `CGWarpMouseCursorPosition`, `CGDisplayFade`, `CGAssociateMouseAndMouseCursorPosition` | **Very many ordinary apps.** Screenshotting, hotkeys, multi-monitor, games grabbing the cursor | **Yes.** These are documented, stable, and unavoidable. This is the part the brief undercounts |
| **T3** | Private `CGSâ€¦`/`SLSâ€¦` direct calls | Window managers (yabai), Dock replacements, a scattering of blur/level tricks in Electron & Qt apps | **No, not faithfully.** Provide a *shim* that maps a small, curated subset onto our protocol; `kCGErrorFailure` the rest |

The economics: T1 costs the same regardless (we're writing AppKit anyway). T2 is bounded â€” a few dozen documented functions with documented semantics, and it *unlocks* apps in a way nothing else does. T3 is unbounded, undocumented, version-skewed, changes every macOS release, and buys us a handful of utilities that are precisely the apps a user would rather run the OpenOSX-native equivalent of.

**Also note the T3 escape hatch:** many apps that "need" T3 actually need *capabilities* (list windows, move a window, register a hotkey, capture the screen). Serve those as **first-class public OpenOSX APIs** and the T3 pressure largely evaporates â€” plus we get an API surface with a sane security model instead of Apple's `CGSSetUniversalOwner`.

**Decision: build T1 on a clean protocol of our own design, implement T2 faithfully against public headers, ship T3 as a best-effort compatibility shim behind a feature flag, and never promise T3 fidelity.**

### 2.2 Process and layer topology

```
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ App process â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚ NSView/NSWindow â”€ AppKit                                    â”‚
â”‚   â†“ (all drawing)                                           â”‚
â”‚ CoreGraphics (our O2-style CGContext â†’ Cairo/Skia)          â”‚
â”‚   â†“ CGWindow / CALayer commit                               â”‚
â”‚ libOWSClient.dylib   â† the ONLY thing that talks to owsd    â”‚
â”‚   â€¢ XPC control channel (window lifecycle, geometry, props) â”‚
â”‚   â€¢ CFMachPort run-loop source (event ring)                 â”‚
â”‚   â€¢ PDSurface handles (pixels â€” never copied over IPC)      â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
               â”‚ launchd bootstrap: "org.openosx.windowserver"
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€ owsd (per login session) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚ Connection table   (OWSConnectionID â†’ windows, surfaces)    â”‚
â”‚ Window tree        (level, order, shape, alpha, shadow)     â”‚
â”‚ Surface registry   (PDSurface, refcounts, seeds)            â”‚
â”‚ Layer/animation engine  (CA-lite, server-side timing)       â”‚
â”‚ Input router       (seats, focus, hit-test, taps, cursor)   â”‚
â”‚ Presentation backend  â—„â”€â”€ pluggable                         â”‚
â”‚   â”œâ”€â”€ OWSBackendWayland   (client of sway â€” Phase 1)        â”‚
â”‚   â”œâ”€â”€ OWSBackendWlroots   (owsd IS the compositor â€” Ph. 6)  â”‚
â”‚   â””â”€â”€ OWSBackendHeadless  (CI, no display â€” build first!)   â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

**`OWSBackendHeadless` should be written before either real backend.** It makes the whole stack testable in the existing CI pipeline with no GPU, no display, no VM â€” you assert on window trees and pixel hashes. Given the project's headless CI/QEMU iterate-loop (task #8), this is the highest-leverage single decision in the plan.

### 2.3 Transport: XPC for control, Mach + shared memory for events

OpenOSX has genuine Mach IPC, launchd, and XPC. Use each where it's strong:

- **Control plane â†’ XPC.** `xpc_connection_create_mach_service("org.openosx.windowserver", â€¦)`. Free launchd bootstrap, on-demand launch, peer credentials (`xpc_connection_get_euid/pid`), typed dictionaries, and it carries **Mach send rights and shared-memory regions** as first-class object types â€” which is exactly how surface handles travel. Do **not** hand-roll MIG: Apple's MIG choice is a 1990s artifact, and MIG's rigidity is a large part of why CGS is unversionable.
- **Event plane â†’ lock-free SPSC ring in shared memory + a Mach port doorbell.** XPC's per-message cost is wrong for 1000 Hz pointer traffic. Server writes `OWSEventRecord`s into a per-connection ring and sends a coalescing wakeup on a Mach port; the client's `CFMachPort` run-loop source drains the ring. This mirrors `CGSGetEventPort` semantics using only public CF primitives, so `NSApplication`'s run-loop integration falls out for free.
- **Pixels â†’ never in a message.** PDSurface IDs only (Â§3).

**Version the protocol from commit one.** `OWSProtocolVersion` negotiated at connect; server supports N and N-1. This is the thing Apple can't do and we can.

### 2.4 The client API â€” define it as an IDL, generate both sides

Put an IDL at `src/Servers/owsd/protocol/ows.proto.txt` (a simple hand-rolled schema + Python generator beats a dependency here) generating `libOWSClient` stubs and `owsd` dispatch. Sketch of the object model:

```c
/* Connection */
OWSConnectionID OWSMainConnectionID(void);
kern_return_t   OWSConnectionCreate(OWSConnectionID *out);
kern_return_t   OWSConnectionRelease(OWSConnectionID);
mach_port_t     OWSGetEventPort(OWSConnectionID);       /* â† CFMachPort source */

/* Windows */
kern_return_t OWSWindowCreate(OWSConnectionID, const OWSWindowDesc*, OWSWindowID *out);
kern_return_t OWSWindowSetFrame(OWSConnectionID, OWSWindowID, CGRect);
kern_return_t OWSWindowSetLevel(OWSConnectionID, OWSWindowID, int32_t);  /* NSWindowLevel */
kern_return_t OWSWindowOrder(OWSConnectionID, OWSWindowID, OWSOrderOp, OWSWindowID rel);
kern_return_t OWSWindowSetOpaqueShape(OWSConnectionID, OWSWindowID, OWSRegionID);
kern_return_t OWSWindowSetAlpha(OWSConnectionID, OWSWindowID, float);
kern_return_t OWSWindowSetShadow(OWSConnectionID, OWSWindowID, const OWSShadowSpec*);

/* Surfaces â€” pixels by reference */
kern_return_t OWSWindowAttachSurface(OWSConnectionID, OWSWindowID, uint64_t pdSurfaceID);
kern_return_t OWSWindowCommit(OWSConnectionID, OWSWindowID,
                              const CGRect *damage, size_t nDamage, uint64_t seed);

/* Atomic multi-window transactions (the CGSDisableUpdate idea, done right) */
kern_return_t OWSTransactionBegin(OWSConnectionID, OWSTransactionID *out);
kern_return_t OWSTransactionCommit(OWSConnectionID, OWSTransactionID);
/* server-side watchdog auto-commits after N ms â€” Apple learned this the hard way */

/* Activation: TWO operations, deliberately separate (see Â§1.2) */
kern_return_t OWSSetKeyWindow(OWSConnectionID, OWSWindowID);       /* focus state */
kern_return_t OWSActivateConnection(OWSConnectionID, OWSActivateOpts); /* visible raise */
```

Three deliberate departures from Apple that cost nothing now and save years later:

1. **Explicit damage + surface seed on every commit.** Apple's implicit-dirty model is a performance tax. Wayland demands damage anyway.
2. **A real capability model.** "May enumerate other connections' windows," "may inject events," "may capture screen" as XPC-entitlement-checked capabilities â€” instead of `CGSSetUniversalOwner`. Cheap now, and it's the *only* time it's cheap.
3. **Async-by-default with explicit barriers.** Apple's CGS is largely synchronous RPC; every window move is a round trip. Make setters async and add `OWSSync()` for the rare cases needing it.

### 2.5 Where AppKit plugs in â€” the backend abstraction

Follow the settled prior art. Both GNUstep and Darling/Cocotron converged on the same answer:

- **GNUstep**: `GSDisplayServer` is *"an abstract class which provides a framework for a device independent window serverâ€¦ basic window creation and handling, event handling, cursors,"* with concrete per-platform subclasses. ([GNUstep](https://www.gnustep.org/resources/documentation/Developer/Gui/Additions/GSDisplayServer.html))
- **Darling/Cocotron**: *"AppKit should make `CGS*` API callsâ€¦ new backends implemented in `cocotron/CoreGraphics/*.backend`."* Note they explicitly moved backends **out of AppKit and into CoreGraphics** â€” because AppKit-level backends leak platform assumptions into view code. ([darling#937](https://github.com/darlinghq/darling/issues/937))

**Adopt the Darling/Cocotron layering:** AppKit is 100% platform-free and speaks only CG/OWS. `libOWSClient` is the backend. Cocotron's O2Graphics split (`O2Context_*` per platform, with a GDI backend on Windows) is the model for our `O2Context_cairo` / `O2Context_pdsurface`. ([Cocoa with Love](https://cocoawithlove.com/2010/04/design-of-multi-platform-app-using.html))

This means our AppKit is portable *by construction* â€” it will run on macOS itself against real CGS via a shim backend, which is an enormous testing advantage: **you can develop and diff our AppKit against Apple's on real hardware.**

### 2.6 Where CoreAnimation lives â€” recommendation

Apple puts the render server inside WindowServer; iOS puts it in `backboardd`. **Recommendation: put a minimal layer engine inside `owsd`, but keep rasterisation in the client for Phase 1â€“4.**

- Phase 1â€“4: client rasterises its whole window into one PDSurface. `owsd` composites *windows*, not layers. Simple, debuggable, correct.
- Phase 5+: introduce `OWSLayerTree` â€” server-side geometry/transform/opacity/animation on *sublayers* that own their own PDSurface. This directly buys the `CALayerHost` capability (a layer whose content is another connection's surface) which is what out-of-process WebKit-style content and video playback will need.
- Do **not** attempt server-side rasterisation of layer *content*. Apple does content rasterisation client-side too; the server composites.

The `CAContextID` concept maps to `OWSLayerContextID`, and it is worth reserving in the protocol now even if unimplemented, because retrofitting cross-process layer embedding is painful.

---

## 3. IOSurface, PDSurface, and dmabuf

### 3.1 Does OpenOSX need an IOSurface equivalent?

**Yes, and it already has one.** `src/Libraries/PDSurface/include/PDSurface.h` (branch `openosx-next`) provides device open, create, dimension/format/stride accessors, CPU base address for linear surfaces, `PDSurfaceFlush(rect)`, `PDSurfaceSetScanout`, and â€” critically â€” `PDSurfaceGetID`/`PDSurfaceLookup` for cross-process handoff. The header's own comment explains the ID design: *Darwin has no dmabuf FD, so an opaque ID over the driver's registry costs less than pretending otherwise.*

The mapping to IOSurface is near-total:

| IOSurface (public) | PDSurface (exists) | Gap |
|---|---|---|
| `IOSurfaceCreate(props)` | `PDSurfaceCreate(dev, desc)` | Properties are a fixed struct, not a CFDictionary â€” fine, arguably better |
| `IOSurfaceGetBaseAddress` | `PDSurfaceGetBaseAddress` | âœ… |
| `IOSurfaceGetBytesPerRow/Width/Height/PixelFormat` | `PDSurfaceGetStride/Width/Height/Format` | âœ… |
| `IOSurfaceLock/Unlock` | *(missing)* | **Gap: no CPU/GPU access synchronisation** |
| `IOSurfaceGetSeed` | *(missing)* | **Gap: no change counter** |
| `IOSurfaceCreateMachPort` / `LookupFromMachPort` | `PDSurfaceGetID` / `PDSurfaceLookup` | **Gap: an integer ID is not a capability.** Any process can guess/enumerate IDs |
| Multi-planar (YUV) | *(missing)* | Gap â€” needed for video, not for Phase 1â€“5 |
| `IOSurfaceCreateXPCObject` | *(missing)* | Wanted, so surfaces ride the XPC control channel |

**Recommended work items on PDSurface, in priority order:**

1. **Mach-port handles.** Add `PDSurfaceCreateMachPort` / `PDSurfaceLookupFromMachPort`. A send right *is* a capability: unforgeable, revocable, transferable through XPC, and it makes surface lifetime track port lifetime. This is the single most important hardening step â€” the current integer ID is an ambient-authority hole in a display server that will handle other users' pixels.
2. **Seed counter + lock/unlock.** Needed for correctness the moment the GPU touches a surface concurrently with the CPU.
3. **`PDSurfaceCreateXPCObject`.** Trivial once (1) exists; makes the control protocol clean.
4. **Explicit format table.** Pin `kPDSurfaceFormat*` to DRM FourCCs so the gbm/wlroots mapping is a memcpy of an enum, not a translation table.
5. **Planar support** â€” defer to the AVFoundation/video phase.

### 3.2 Mapping to dmabuf/gbm

The Linux-side analogy is exact: *"On Linux, the DMA buffer (dmabuf) is the equivalent to IOSurface used on macOS"* ([Blaztinn](https://blaztinn.gitlab.io/post/dmabuf-texture-sharing/)); GBM allocates buffers shared between EGL rendering and KMS display ([kernel.org](https://docs.kernel.org/driver-api/dma-buf.html)).

The important subtlety: **OpenOSX's libgbm/libdrm are already shims** (`nix/pkgs/x11/libgbm.nix`, `libdrm.nix`), and Mesa is driven through a PureDarwin virgl winsys (`nix/pkgs/mesa/virgl-puredarwin/virgl_puredarwin_winsys.c`). So we are not mapping PDSurface *onto* real dmabuf â€” **we own both ends of the shim.** The layering should be:

```
Mesa / wlroots / GTK
        â†“  (unmodified upstream expectations)
libgbm shim  â”€â”€â”
libdrm shim  â”€â”€â”¼â”€â”€â–º  PDSurface  â”€â”€â–º  IOGOPFramebuffer / virtio-gpu kext
Wayland linux-dmabuf â”€â”€â”˜
```

- `gbm_bo` becomes a thin wrapper over `PDSurfaceRef`; `gbm_bo_get_fd()` is the one call with no honest answer.
- **Where a dmabuf FD is unavoidable** (the `linux-dmabuf` Wayland protocol transports FDs; wlroots expects them), two options: (a) implement a **`zwp_linux_dmabuf`-shaped OpenOSX protocol extension** carrying PDSurface Mach ports instead of FDs, and patch wlroots' allocator to use it; or (b) fall back to `wl_shm` (a plain shared-memory pool over a Mach-mapped region), which costs a copy but works today.
  - **Recommendation: ship (b) for Phase 1â€“4, land (a) before Phase 6.** A `wl_shm` blit of a 1080p window is ~8 MB/frame â€” ugly but perfectly usable for bring-up, and it removes an entire class of blocking work from the critical path to first pixels.
- `PDSurfaceSetScanout` already gives us the direct-scanout path for the native-compositor backend â€” that's the fullscreen-app fast path most compositors take years to add.

**Important framing:** because we own `owsd`, our AppKit, *and* the gbm/dmabuf shims, an OpenOSX-native app's pixels can travel **app â†’ PDSurface â†’ owsd â†’ scanout with zero copies and zero Wayland involvement** in the native-compositor mode. The Wayland path is a *transitional compatibility mode*, not the destination. Say this in the design doc so nobody optimises the wrong path.

---

## 4. Aqua look-and-feel vs X11/Wayland conventions

### 4.1 The semantic mismatches (these are the hard part, not the pixels)

| Aqua / AppKit | X11 / Wayland | Consequence |
|---|---|---|
| **Global menu bar** â€” the menu belongs to the *application*, lives at screen top, changes on app activation | Per-window menus inside the client's own surface | Needs a shell component + a menu protocol; no Wayland equivalent |
| **App-level activation** â€” activating an app raises *all* its windows; app stays running with zero windows | Window-level focus; no app concept in xdg-shell | Need an app/connection abstraction above toplevels |
| **key window vs main window** â€” *"there can be only one main window per app"* and *"the system gives main, key, and inactive windows different appearances"* ([Apple HIG](https://developers.apple.com/design/human-interface-guidelines/components/presentation/windows/)) | One binary focus state | Two orthogonal focus bits must be modelled server-side and reflected in chrome |
| **Window levels** â€” normal/floating/modal-panel/status/popup/screensaver as a numeric stacking hierarchy | Wayland: xdg-shell has *no* client-settable stacking; layer-shell has 4 coarse layers | Levels must live in `owsd`, not be delegated to sway |
| **Sheets** â€” *"the sheet unfurls from the window's title bar"* ([Apple HIG](https://developers.apple.com/design/human-interface-guidelines/components/presentation/windows/)) | Modal dialogs are independent toplevels with a parent hint | Needs owsd-side parent-anchored positioning + animation |
| **Server-drawn shadows, rounded corners, vibrancy/blur, non-rectangular opaque shapes** | Wayland: client draws its own shadow into a padded surface; blur only via non-standard protocols | We must do this in owsd/compositor; via sway it degrades |
| **Coordinate system: origin bottom-left, Y-up** | Top-left, Y-down everywhere | One flip, one place. Put it in `libOWSClient` and never think about it again |
| **Command key as the primary modifier** | Ctrl primary; Super owned by the WM | Modifier remap + a policy for WM-reserved chords |
| **Backing scale factor** (integer 1x/2x) | Wayland fractional scaling | Constrain to integer scale in Phase 1â€“5 |

The two genuinely deep ones are **global menu bar** and **key/main window separation**. Everything else is drawing.

### 4.2 The compositor: what "forking XFCE" should actually mean

Given the tree is sway + wlroots + xfce4-panel/xfdesktop on layer-shell:

**Phase 1â€“5 (hosted):** don't fork anything. `owsd` is a Wayland *client*. Use `xdg-shell` with **client-side decorations that we draw** â€” CSD is the right call because Aqua chrome is intricate (traffic lights, unified toolbars, proxy icons, sheet unfurling) and we already have a full CoreGraphics rasteriser in-process. Also request `xdg-decoration` mode `client` so sway doesn't add its own. This gets Aqua-looking windows on screen *without touching the compositor at all*.

**Phase 6 (native): replace sway with `openosx-compositor` written against wlroots.** wlroots is a library â€” `wlr_xdg_shell`, `wlr_layer_shell_v1`, `wlr_foreign_toplevel_management_v1`, `wlr_scene` are components you assemble ([wlroots docs](https://kennylevinsen.pages.freedesktop.org/wlroots/wlr/types/wlr_xdg_shell.html), [Drew DeVault](https://drewdevault.com/blog/Wayland-shells/)). A focused Aqua compositor is on the order of a few thousand lines. Crucially, **`owsd` and the compositor should be the same process** in this mode: window levels, key/main state, shadows, and Space switching all become local function calls instead of a protocol negotiation with a compositor that has different opinions. Legacy Wayland/X11 apps (GTK, Xwayland) remain first-class clients of it.

**The global menu bar:** a `wlr-layer-shell` surface anchored `top`, exclusive zone = menubar height, on the `top` layer. `gtk-layer-shell` is already packaged, so a first implementation can even be a GTK program. It subscribes to `owsd` for "active connection changed â†’ here is its NSMenu tree," rendering it with our own AppKit. For *foreign* (GTK/Qt) apps, `wlr-foreign-toplevel-management` gives title/app-id/state so the menubar can at least show a correct app name and a synthesized minimal menu â€” the same trick Unity's appmenu and GNUstep's DBusMenu bridge use ([labwc integration](https://labwc.github.io/integration.html), [GNUstep DKMenuRegistry](https://github.com/gnustep/libs-dbuskit/blob/master/Bundles/DBusMenu/DKMenuRegistry.m)).

**xfce4-panel's fate:** it stays through Phase 5 as the working desktop. The OpenOSX menubar ships alongside it, then replaces it. Don't fork xfce4-panel to make it look like a menubar â€” a menu bar is not a panel; it is an AppKit view of the active app's `NSMenu`. Write it natively; that's the identity DE.

**Traffic lights and titlebars:** drawn by our AppKit's `NSThemeFrame` equivalent, in-process, from a theme description. Because they're CSD, they're pixel-identical in both hosted and native modes â€” which means Phase 5 work isn't thrown away at Phase 6.

---

## 5. Phased plan: from "a window appears" to "the app is interactive"

Each phase has a hard exit criterion that can be asserted in headless CI.

### Phase 0 â€” Foundations (no pixels)
- Write `docs/design/owsd-protocol.md` and the IDL; codegen both sides.
- Harden PDSurface: Mach-port handles, seed, lock/unlock, DRM-FourCC formats.
- Stand up `OWSBackendHeadless` + a pixel-hash test harness in the existing CI.
- Institute `CONTRIBUTING-CLEANROOM.md`.
- **Exit:** `owsd` runs headless under launchd; a test client creates a window, attaches a PDSurface, commits damage; CI asserts the resulting window tree and surface hash. **Zero graphics involved.**

### Phase 1 â€” First pixels (window appears with correct contents)
- `OWSBackendWayland`: one `OWSWindow` â†’ one `xdg_toplevel`; PDSurface â†’ `wl_shm` pool â†’ `wl_buffer`; damage â†’ `wl_surface.damage_buffer`.
- Client-side: a raw C test app filling a gradient.
- **Exit:** a non-AppKit test client shows a correctly-coloured, correctly-sized window inside the existing sway/XFCE session, with damage-driven updates.

### Phase 2 â€” CoreGraphics on top
- `CGBitmapContext`/`CGContext` rasterising into a PDSurface (Cairo backend, Cocotron-O2-style layering).
- `CGWindow`-level API bound to `owsd`.
- **Exit:** a client draws with public CG calls (paths, text via CoreText, images) and it appears correctly. Golden-image tests in CI.

### Phase 3 â€” AppKit lights up (static UI)
- `NSApplication`, `NSWindow`, `NSView` hierarchy, `NSThemeFrame` chrome, `-drawRect:` â†’ CG â†’ PDSurface.
- `libOWSClient` as the sole backend; AppKit contains zero platform code.
- **Exit:** an `.app` bundle with a `NSWindow` containing `NSButton`/`NSTextField`/`NSImageView` renders correctly. Not yet clickable.

### Phase 4 â€” Interactivity (the brief's finish line)
- Input router in `owsd`: seat model, hit-testing against window shapes respecting level/order, cursor.
- Wayland `wl_pointer`/`wl_keyboard` â†’ `OWSEventRecord` â†’ `CGEventRef` â†’ `NSEvent`. **Preserve the Darling invariant:** everything is an `OWSEventRecord` first.
- Keyboard: xkb keysyms â†’ Apple virtual keycodes; `UCKeyTranslate`/`.keylayout` for characters; **Superâ†’Command** remap with a documented WM-chord reservation list.
- Focus: implement `key` and `main` as separate bits; `OWSSetKeyWindow` vs `OWSActivateConnection` as separate ops.
- Run-loop: `CFMachPort` source draining the event ring; `NSApp.sendEvent:` â†’ `NSWindow.sendEvent:` â†’ responder chain.
- **Exit:** click a button and its action fires; type into an `NSTextField` and characters appear; drag the titlebar and the window moves; Cmd+Q quits. **This is "app is interactive."**

### Phase 5 â€” Aqua identity
- Full Aqua chrome: traffic lights with hover/active states, unified toolbar, sheets anchored to the parent titlebar, key/main/inactive appearance differentiation.
- Global menubar as a `wlr-layer-shell` surface fed by `owsd` activation events; `wlr-foreign-toplevel-management` bridging for GTK/Qt apps.
- `NSWindowLevel` enforced in `owsd`'s own stacking.
- **Exit:** a screenshot of the session is recognisably Aqua; the menubar tracks app activation; sheets unfurl.

### Phase 6 â€” Native compositor + zero-copy
- `openosx-compositor`: wlroots-based, **same process as `owsd`**. sway retired.
- `linux-dmabuf`-equivalent protocol carrying PDSurface Mach ports; wlroots allocator patched. Zero-copy path live.
- Server-side shadows, rounded corners, blur; `PDSurfaceSetScanout` fullscreen fast path.
- **Exit:** frame-perfect resize, no `wl_shm` copies for OpenOSX apps, GTK/Xwayland apps still work.

### Phase 7 â€” Public CG compatibility (the T2 surface)
- `CGDisplay*`, `CGGetActiveDisplayList`, `CGWindowListCopyWindowInfo`, `CGDisplayCreateImage`, `CGEventTapCreate`, `CGWarpMouseCursorPosition` â€” all served by `owsd` behind the capability model.
- **Exit:** a third-party screenshot utility and a third-party global-hotkey utility work unmodified.

### Phase 8 â€” CoreAnimation & the CGS shim
- `OWSLayerTree`: server-side sublayer geometry/transform/opacity + declarative animation; `CALayerHost` equivalent via `OWSLayerContextID`.
- `libCGSCompat`: curated `CGSâ€¦`/`SLSâ€¦` subset mapped onto OWS; everything else returns `kCGErrorFailure`. Feature-flagged, explicitly unsupported.
- **Exit:** a `CAAnimation` runs server-side; an app calling `CGSSetWindowBackgroundBlurRadius` doesn't crash.

**Rough sequencing note:** Phases 0â€“4 are the load-bearing ones and are largely serial. Phase 5 can overlap Phase 4. Phase 6 is independent of Phase 7 and they can run in parallel by different people.

---

## 6. Licensing matrix for shippable components

| Component | License | Verdict |
|---|---|---|
| **Cocotron** (AppKit/Foundation/O2Graphics) | MIT | âœ… **Vendor freely.** The single best starting point for AppKit + the CG backend split |
| **Darling's Cocotron fork** | MIT-derived (verify per-file; Darling *overall* is GPL-3) | âš ï¸ Vendor only files provably MIT-lineage. Audit per file, record provenance |
| **Darling proper** (darlingserver, AppKit work) | GPL-3 | âŒ **Study the issue tracker/design docs only.** Do not vendor. Ideally, different people read it than write ours |
| **GNUstep libs-gui/libs-base** | LGPL-2.1+ | âš ï¸ Linkable as a dylib in an APSL/BSD image, but LGPL relinking obligations attach to the shipped image. Best used as a *behavioural reference* (`GSDisplayServer` design) rather than vendored |
| **wlroots** | MIT | âœ… Ideal for `openosx-compositor` |
| **sway** | MIT | âœ… Fine to ship transitionally and to read |
| **Cairo** | LGPL-2.1 / MPL-1.1 dual | âœ… MPL branch avoids LGPL relinking issues â€” elect MPL explicitly |
| **Skia** (alternative rasteriser) | BSD-3 | âœ… Cleanest license; much heavier build. Consider for Phase 6+ if Cairo perf disappoints |
| **NUIKit/CGSInternal** | MIT | âœ… Usable as a tier-B *specification of names*. Do not ship |
| **PDSurface / PDGOP / virgl shim** | BSD-3 (per `pdsurface.nix`) | âœ… Already ours-compatible |
| **ravynOS** | Mixed; site asserts "All Rights Reserved" over the project | âš ï¸ Read the public design discussion; check per-repo license before touching any code |

**One-line rule for the team:** *if it isn't MIT, BSD, APSL, or MPL, it doesn't go in the image â€” and Darling's source doesn't go in your eyeballs if you're writing AppKit.*

---

## 7. Risks and things I'd flag now

1. **Text input / IME is not in any phase above and it is a whole subsystem.** On macOS it isn't WindowServer at all â€” it's TSM/InputMethodKit talking to a separate input-method process. Wayland has `text-input-v3`/`input-method-v2`. Budget a Phase 4.5. CJK users hit this immediately.
2. **Pasteboard is not WindowServer either** (`pbs` on macOS). Drag-and-drop, however, *is* partly a WindowServer concern (drag sessions cross process boundaries with a server-tracked drag image). Plan `NSPasteboard` as a separate XPC service that bridges `wl_data_device`.
3. **`wl_shm` copies will make Phase 1â€“5 feel slow on a VM.** That is expected and acceptable. Do not let it trigger a premature dive into the dmabuf work â€” put a note in the docs so it doesn't get relitigated every month.
4. **Surface leaks in the server** are a known IOSurface-shaped failure mode ([cmux#1435](https://github.com/manaflow-ai/cmux/issues/1435)). Connection-scoped reclamation + a `owsctl surfaces` introspection command from day one.
5. **`owsd` is a privilege boundary.** WindowServer has a long CVE history precisely because it's a highly-privileged MIG server reachable from every sandbox ([RET2](https://blog.ret2.io/2018/08/28/pwn2own-2018-sandbox-escape/)). Using XPC (typed, memory-safe deserialization) instead of hand-rolled MIG removes the largest historical bug class. Say so in the design doc â€” it's a genuine architectural win over Apple, not just a convenience.
6. **Testing our AppKit against Apple's is legitimate and valuable.** Because the backend is pluggable, our AppKit can run *on macOS* against real CGS. Differential-testing rendering and event semantics against the real thing is tier-B (observable behaviour) and completely clean. This should be an explicit CI target once a Mac build node exists (task #14).
7. **Don't let Metal/Swift scope-creep in.** Neither is on the critical path to "app is interactive." Metal in particular is a multi-year project on its own; note it and move on.

---

## 8. Concrete next actions

| # | Action | Where |
|---|---|---|
| 1 | Write `docs/design/owsd-protocol.md` â€” object model, IDL, versioning, capability model | new |
| 2 | Harden PDSurface: Mach-port handles, seed, lock/unlock, DRM FourCCs | `src/Libraries/PDSurface/` (branch `openosx-next`) |
| 3 | Scaffold `src/Servers/owsd/` with `OWSBackendHeadless` + CI pixel-hash harness | new |
| 4 | Vendor Cocotron (MIT) as `src/Frameworks/AppKit` + `src/Frameworks/CoreGraphics`, backends under `CoreGraphics/*.backend` per darling#937 layering | new |
| 5 | Add `CONTRIBUTING-CLEANROOM.md` + per-file provenance comment convention | repo root |
| 6 | Correct task #13 from "Fork XFCE" to "Write `openosx-compositor` on wlroots; retire sway" | task list |

**Files referenced (branch `openosx-next`, not checked out on current `openosx-dev`):**
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\src\Libraries\PDSurface\include\PDSurface.h`
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\src\Libraries\PDSurface\PDSurface.c`
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\x11\pdsurface.nix`
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\wayland\wlroots.nix`, `sway.nix`, `gtk-layer-shell.nix`
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\xfce\sway-config`, `xfce4-session-sway-compositor.patch`
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\mesa\virgl-puredarwin\virgl_puredarwin_winsys.c`
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\src\Kernel\Extensions\IOGOPFramebuffer\IOGOPFramebuffer.cpp`

---

## Sources

- [Inside macOS window internals: how SkyLight enables multi-cursor background agents â€” Cua](https://cua.ai/blog/inside-macos-window-internals)
- [WindowServer: The privilege chameleon on macOS (Part 1) â€” Keen Security Lab](https://keenlab.tencent.com/en/2016/07/22/WindowServer-The-privilege-chameleon-on-macOS-Part-1/)
- [NUIKit/CGSInternal â€” CGSConnection.h](https://github.com/NUIKit/CGSInternal/blob/master/CGSConnection.h)
- [CGWindowServerCFMachPort() â€” Apple Developer Documentation](https://developer.apple.com/documentation/coregraphics/cgwindowservercfmachport())
- [IOSurface â€” Apple Developer Documentation](https://developer.apple.com/documentation/iosurface)
- [Cocoa Event Handling Guide: Event Architecture â€” Apple](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/EventArchitecture/EventArchitecture.html)
- [Main event loop â€” Apple (archived)](https://developer.apple.com/library/archive/documentation/General/Devpedia-CocoaApp-MOSX/MainEventLoop.html)
- [Windows â€” Apple Human Interface Guidelines](https://developers.apple.com/design/human-interface-guidelines/components/presentation/windows/)
- [Cross-process Rendering â€” Russ Bishop](http://www.russbishop.net/cross-process-rendering)
- [Understanding WindowServer on macOS â€” Andrea Fortuna](https://andreafortuna.org/2025/10/05/macos-windowserver/)
- [The Secret Life of Core Animation â€” Aditya Vaidyam](https://avaidyam.github.io/2018/02/22/SecretLife_CoreAnimation.html)
- [ios-rendering-docs â€” Ethan Arbuckle](https://github.com/EthanArbuckle/ios-rendering-docs)
- [Cross-Process Rendering using CALayer â€” TeamDev](https://medium.com/teamdev-engineering/cross-process-rendering-using-calayer-885dd2a94c1e)
- [Cocotron (AppKit) backend rework â€” darling#937](https://github.com/darlinghq/darling/issues/937)
- [Design of a multi-platform app using The Cocotron â€” Cocoa with Love](https://cocoawithlove.com/2010/04/design-of-multi-platform-app-using.html)
- [GSDisplayServer â€” GNUstep](https://www.gnustep.org/resources/documentation/Developer/Gui/Additions/GSDisplayServer.html)
- [GNUstep DBusMenu registry â€” libs-dbuskit](https://github.com/gnustep/libs-dbuskit/blob/master/Bundles/DBusMenu/DKMenuRegistry.m)
- [Writing a Wayland compositor with wlroots: shells â€” Drew DeVault](https://drewdevault.com/blog/Wayland-shells/)
- [wlr_xdg_shell.h â€” wlroots documentation](https://kennylevinsen.pages.freedesktop.org/wlroots/wlr/types/wlr_xdg_shell.h.html)
- [XDG shell basics â€” The Wayland Protocol](https://wayland-book.com/xdg-shell-basics.html)
- [Integration (layer-shell, foreign-toplevel) â€” labwc](https://labwc.github.io/integration.html)
- [Buffer Sharing and Synchronization (dma-buf) â€” Linux Kernel docs](https://docs.kernel.org/driver-api/dma-buf.html)
- [Inter-Process Texture Sharing with DMA-BUF â€” Blaztinn](https://blaztinn.gitlab.io/post/dmabuf-texture-sharing/)
- [macOS IPC â€” HackTricks](https://hacktricks.wiki/en/macos-hardening/macos-security-and-privilege-escalation/macos-proces-abuse/macos-ipc-inter-process-communication/index.html)
- [Exploiting the macOS WindowServer for root â€” RET2 Systems](https://blog.ret2.io/2018/08/28/pwn2own-2018-sandbox-escape/)
- [The macOS Process Journey â€” WindowServer â€” Shlomi Boutnaru](https://medium.com/@boutnaru/the-macos-process-journey-windowserver-file-system-events-daemon-3b619f276fd1)
- [ravynOS â€” Technical Details](https://ravynos.com/more/)
- [ravynOS PHILOSOPHY.md](https://github.com/ravynsoft/ravynos/blob/main/PHILOSOPHY.md)
- [Darling â€” macOS translation layer for Linux](https://www.darlinghq.org/)
- [High memory usage: IOSurface GPU buffers accumulate â€” cmux#1435](https://github.com/manaflow-ai/cmux/issues/1435)
