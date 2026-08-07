# OpenOSX Desktop Integration Research: Forking XFCE as the Identity DE

**Scope:** how to turn the in-tree XFCE 4.20 stack into OpenOSX's own desktop, how Aqua-style window chrome and a global menubar actually get built, and which display protocol the AppKit/WindowServer compat layer should target.

**Ground truth from the repo** (`origin/openosx-next`), which shaped every recommendation below:

| In tree | Version / note |
|---|---|
| `nix/xfce.nix` | xfconf 4.20.0, libxfce4util, libxfce4ui 4.20.2, **xfwm4 4.20.0**, libxfce4windowing 4.20.6, garcon 4.20.0, exo 4.20.0, xfce4-session 4.20.4, xfce4-panel 4.20.8, xfdesktop 4.20.2, xfce4-terminal 1.2.0, xfce4-settings 4.20.5, xfce4-appfinder 4.20.0, thunar 4.20.9 |
| `nix/pkgs/x11/xorg.nix` | real Xorg **21.1.24**, cross-built for Darwin |
| `nix/pkgs/wayland/` | wayland, wayland-protocols, wlroots, **sway**, gtk-layer-shell, xwayland |
| `nix/pkgs/xfce/sway-config` | Wayland session = sway as compositor, panel + xfdesktop via layer-shell |
| xfwm4 configureFlags | `--enable-compositor --enable-xpresent --enable-epoxy --enable-render --enable-randr --enable-xsync`, XI2 **off** |

So there are already two working session shapes: **X11 (Xorg + xfwm4 + panel)** and **Wayland (sway + panel/xfdesktop over layer-shell)**. That fact matters for section 4.

---

## 1. What to fork, what to track, what to replace

### 1.1 The strategic split

The single most useful framing I can offer: **do not fork XFCE uniformly.** Split it by *what kind of thing each component is*.

- **Pixel-pushing components** (window manager/compositor, panel, desktop) â€” these define the look and must be forked, because their behaviour is not configurable into Aqua.
- **Policy/plumbing components** (session manager, settings daemon, menu database, app launcher) â€” these should be **retired and replaced with native Darwin services OpenOSX already has**: launchd, CFPreferences, LaunchServices-over-`.app` bundles. This is where "its own OS rather than XFCE with a skin" is actually won, and as a bonus it removes GPL code from the critical path.
- **Applications** (terminal, file manager, appfinder) â€” track upstream with a thin rebranding patch, and replace with native AppKit apps opportunistically, one at a time, once AppKit exists.

### 1.2 Component-by-component

| Component | License | Disposition | Rationale |
|---|---|---|---|
| **xfwm4** | GPLv2 | **Hard fork** | Becomes OpenOSX's window manager + compositing manager, and later merges into WindowServer. Needs deep code changes (levels, CSD policy, hotkey routing, ExposÃ©). Upstream is now X11-only *forever* (see Â§4), so divergence costs nothing. |
| **xfce4-panel** | GPLv2 + LGPLv2.1 (COPYING.LIB) | **Hard fork** â†’ menu bar | Becomes the global menubar host. The `XfcePanelPlugin` external-plugin ABI is worth keeping (free third-party plugins), the panel's own layout model is not. |
| **xfdesktop** | GPLv2 | **Fork (light)** | Wallpaper + desktop icons + root menu. Icon layer should eventually be re-hosted on LaunchServices/`.app` semantics, but this is a small, stable codebase â€” cheap to carry. |
| **xfce4-session** | GPLv2 | **Retire** | Replace with **launchd** LaunchAgents + a native `loginwindow`-equivalent. launchd is already in tree; running a second session manager on top of it is architecturally redundant and is a real source of ordering bugs. |
| **xfconf / xfconfd** | GPLv2 + LGPLv2.1 | **Retire the daemon, keep the D-Bus interface** | See Â§1.4 â€” this is the highest-leverage move in this whole report. |
| **garcon** | LGPLv2 (docs GFDL 1.1) | **Retire** | freedesktop `.desktop` menu spec is the wrong model for a bundle-based OS. Replace with a LaunchServices-style database over `/Applications/*.app`, plus a `.desktop` importer for cross-built Linux apps. |
| **thunar** | GPLv2 | **Track upstream** | Large, mature, actively developed. Rebrand-only patch queue until a native file manager exists. |
| **exo** | GPLv2 + LGPLv2.1 | **Track** | Thunar dependency; no identity value. |
| **libxfce4util / libxfce4ui** | **LGPLv2** | **Track** | Libraries, LGPL, no identity value. Note LGPLv2 (the old *Library* GPL), not v2.1 â€” see Â§1.3. |
| **libxfce4windowing** | LGPLv2-ish | **Track, then absorb** | Its X11/Wayland abstraction is exactly the shape of the backend split you need in WindowServer. Study it as a model even where you don't link it. |
| **xfce4-settings / xfsettingsd** | GPLv2 | **Fork the daemon, retire the UI** | `xfsettingsd` is the XSettings publisher â€” you need it (or a native replacement) to drive GTK theming, `Gtk/ShellShowsMenubar`, cursor, DPI. The settings *dialogs* should be native AppKit System Settings panes. |
| **xfce4-terminal / xfce4-appfinder** | GPLv2 | **Track, then replace** | First candidates for native AppKit replacements (Terminal.app, Spotlight-equivalent). |
| **tumbler** | GPLv2 (headers inconsistent per Xfce's own audit) | **Track or drop** | Thumbnailing; QuickLook-equivalent is the eventual native answer. |

### 1.3 Licensing: what actually bites

**The easy parts.** GPLv2 does not restrict rebranding. You may rename binaries, change the UI, ship it as OpenOSX. Obligations are mechanical: keep copyright notices and `COPYING`, mark changed files as modified (GPLv2 Â§2(a)), and offer corresponding source for everything you ship (Â§3). Your CI already builds from source, so publishing per-release source tarballs alongside images closes Â§3 cheaply. Avoid using the Xfce name or logo in branding â€” that's a trademark/endorsement question, entirely separate from the copyright licence, and renaming solves it anyway.

**The part that needs a decision, and eventually a lawyer.** APSL 2.0 is **not** GPL-compatible (per the FSF's own compatibility list). Your image mixes APSL/BSD system code with GPLv2 desktop code. Three sub-issues, in descending order of risk:

1. **Linking GPLv2 desktop code against APSL libSystem.** This is the classic "GPL app on a non-GPL-compatible libc" problem. GPLv2's system-library exception exists for exactly this, but its "unless that component itself accompanies the executable" clause is what makes it arguable when *you* ship both. Practical mitigations: (a) Apple shipped GPLv2 userland (bash, gcc) linked against libSystem for two decades without challenge, and PureDarwin has shipped GPL software on this exact base; (b) the copyright holders are the Xfce developers, who are not hostile; (c) prefer *-or-later* components where available. **Recommendation:** document the position in a `LICENSING.md`, ship source, and get a one-time opinion before any 1.0 press push. Do not let it block engineering.
2. **Linking OpenOSX-authored code into GPL processes.** This one you fully control, and the answer is: **never do it.** WindowServer, AppKit, CoreGraphics must be APSL/BSD/MIT and must reach the window manager only over a **wire protocol** (X11, Wayland, D-Bus) â€” never a shared library. This is a hard architectural rule, and conveniently it is also the right architecture regardless of licensing. Every recommendation in Â§3 and Â§4 respects it.
3. **LGPL in a dyld shared cache.** When you eventually build a shared cache, remember LGPL (libxfce4util/ui, and GNUstep if you ever use it) requires the user be able to relink against a modified version. A frozen cache that is the *only* copy of the library is a compliance problem. Keep the standalone dylibs on disk alongside the cache.

**For the compat layer specifically** (per your constraint):
- **Cocotron â€” MIT.** Safe to vendor directly into shippable image components. This is the one to lean on for AppKit reference implementation.
- **GNUstep â€” LGPLv2.1 (libs-gui, libs-base), tools GPLv3+.** Dynamically linkable, but the LGPL relink obligation propagates into your packaging forever, and you'd be shipping a *GPL-family* framework as a *system* framework. Prefer reading it for behavioural insight over vendoring; if you must vendor, vendor at arm's length as a separately-replaceable dylib. **Never link the GPLv3+ tools.**
- **Darling â€” mixed, GPL components.** Study only. Do not vendor into shippable image components.
- **wlroots â€” MIT.** Already in tree, and safe to build on. This matters a lot in Â§4.

### 1.4 The highest-leverage single change: replace xfconfd with a CFPreferences-backed daemon

OpenOSX already has CoreFoundation 1338, so it already has `CFPreferences` and the `~/Library/Preferences/*.plist` model, plus `defaults(1)`. Meanwhile the entire XFCE stack reads and writes settings through `xfconfd` over the `org.xfce.Xfconf` D-Bus interface.

**Write `openosxsettingsd`: a native, APSL/BSD daemon that implements the `org.xfce.Xfconf` D-Bus interface (and XSettings) on top of CFPreferences.** Then drop `xfconfd` entirely.

Why this is worth doing early:

- **It's clean-room by construction.** A D-Bus interface is a published wire protocol with public introspection XML. Reimplementing it is exactly the legitimate-sources posture you've committed to, and it's the same move Wine/ReactOS make.
- **It removes GPL code from the deepest plumbing layer** â€” every XFCE component depends on xfconf, so this is the one substitution that de-risks the most.
- **It unifies the settings story.** `defaults write com.openosx.WindowManager ...` and the desktop's own settings become one system with one file format, one sync model, one backup story. That is a genuinely *OS-level* integration that no XFCE respin has, and users will feel it immediately.
- **It's small.** The xfconf interface is a property-tree get/set/list/reset with change notifications. A few thousand lines.
- **It preserves upstream compatibility**, so tracking thunar/exo/xfce4-settings stays cheap.

Do the same trick for the session: XFCE components mostly just need XSMP-ish startup ordering, which launchd already does better. `xfce4-session` also currently carries a local patch (`xfce4-session-sway-compositor.patch`) â€” that patch disappears if launchd owns session startup.

---

## 2. Aqua-style window decorations on xfwm4

### 2.1 What the xfwm4 theme engine can and cannot do

xfwm4 decorations are **pixmap-based**: a directory of XPM images (PNG with alpha may overlay since 4.2) plus a `themerc` text file. Per-element files exist for `title-1..5-{active,inactive}`, `top-left`, `top-right`, `left`, `right`, `bottom`, `bottom-left`, `bottom-right`, and for each button in `{active, inactive, prelight, pressed}` states.

`themerc` keys that matter here:

```
button_layout=CHM|
button_offset=8
button_spacing=8
title_horizontal_offset=...
title_vertical_offset_active=...
title_shadow_active=false
full_width_title=true
show_app_icon=false
maximized_offset=0
frame_border_top=0            # 4.14.1+
active_text_color=...
```

`button_layout` codes: `O` menu, `T` stick, `H` hide (minimize), `S` shade, `M` maximize, `C` close, `|` title separator.

**What you get for free â€” more than expected:**
- `button_layout=CHM|` puts close / minimize / zoom on the **left**, in macOS order, with the title to the right. Correct semantics, zero code.
- The `prelight` state gives you **glyph-on-hover** behaviour, which is precisely the macOS traffic-light interaction.
- Corner pixmaps with alpha + the compositor (already enabled: `--enable-compositor --enable-render`) give **antialiased rounded top corners**. This is how existing macOS-look xfwm4 themes (elcapitan-xfwm, WhiteSur, B00merang macOS) do it, and they demonstrably work.
- The compositor draws configurable drop shadows and supports ARGB visuals and per-window opacity.
- Double-click-titlebar-to-zoom is already a configurable `double_click_action`.

**Hard limits of the theme format:**
- Raster only â€” no SVG, no vector. Every DPI/scale needs pre-rendered assets.
- **No per-window variation.** One theme, applied to everything.
- Fixed button vocabulary â€” you cannot add a button type without patching xfwm4.
- No procedural drawing: decorations are blitted images, so no gradients computed from the window's accent colour, no live blur.
- Hover/prelight is strictly **per-button** â€” the macOS behaviour where hovering *any* traffic light reveals glyphs on *all three* is not expressible.
- Titlebar height is fixed by the theme; there is no unified-titlebar/toolbar-merge concept at all, because that requires the client and the frame to be the same surface.

### 2.2 The recommendation: dual path, CSD for AppKit

**Do not try to make xfwm4 draw Aqua for macOS apps.** Instead:

**Path A â€” AppKit apps use client-side decorations.** OpenOSX's AppKit draws its own window frame (this is literally how real macOS works: `NSThemeFrame` is client-side; the WindowServer composites, it does not draw chrome). AppKit windows request undecorated frames via `_MOTIF_WM_HINTS` on X11 (or `xdg-decoration` client mode on Wayland), and initiate interactive move/resize via `_NET_WM_MOVERESIZE` / `xdg_toplevel.move`.

This resolves four problems at once:
1. **Unlimited fidelity** â€” unified titlebars, toolbar merging, sheets, variable titlebar heights, vibrancy, per-window accent tinting, group hover on traffic lights. All trivially expressible in your own drawing code, all impossible in `themerc`.
2. **Zero GPL entanglement** â€” the chrome lives in APSL/BSD AppKit, not in forked GPL xfwm4.
3. **Semantic correctness** â€” `NSWindow`'s `styleMask`, `titlebarAppearsTransparent`, `NSWindowStyleMaskFullSizeContentView`, `NSToolbar` integration are *client* concepts. They cannot be faithfully represented by a server-side frame no matter how good the theme.
4. **Backend portability** â€” CSD behaves identically on X11 and Wayland, so the Â§4 migration doesn't rewrite your window chrome.

**Path B â€” the forked xfwm4 provides OpenOSX-styled SSD for everything else.** GTK/X11/legacy apps in the image still need to look like they belong. Ship one first-party pixmap theme, using `button_layout=CHM|`, alpha corner pixmaps, and prelight glyphs. This is a *design asset* task, not an engineering task.

### 2.3 xfwm4 fork changes actually required

Even with CSD, the fork needs real code changes:

1. **Suppress compositor shadows for CSD windows.** AppKit draws its own shadow (and needs to, for correct rounded-corner/inactive-window falloff). Add a window-type/property check so the compositor skips its shadow for windows carrying an OpenOSX marker atom. Without this you get double shadows.
2. **Honour NSWindowLevel.** Map `NSFloatingWindowLevel`, `NSModalPanelWindowLevel`, `NSStatusWindowLevel`, `NSPopUpMenuWindowLevel`, `NSScreenSaverWindowLevel` onto stacking layers. `_NET_WM_WINDOW_TYPE_{UTILITY,DIALOG,DOCK,MENU}` gets you partway; a private `_OPENOSX_WINDOW_LEVEL` atom with a proper N-layer stacking model in the WM gets you all the way. xfwm4's stacking code (`stacking.c`) already has layer concepts to extend.
3. **Application-level focus, not window-level.** macOS activates *applications*; closing all windows keeps the app active and its menus in the bar. xfwm4 has no such concept. The WM must report focus changes to WindowServer with app identity, and must accept "activate this app" requests. (In practice WindowServer should be the authority here â€” see Â§3.4.)
4. **Un-grab the Command key.** Super/Mod4 must be released to applications so `Cmd-Q`, `Cmd-W`, `Cmd-,` reach AppKit. Strip conflicting default keybindings; route `Cmd-Tab` to *your* app switcher, not xfwm4's window cycler (app-level vs window-level switching is a visible identity difference).
5. **Group prelight** for Path B's traffic lights (patch the button prelight to be group-scoped) â€” optional, but it's the detail everyone notices.
6. **Sheets** (`NSWindow beginSheet:`) â€” a child window slid out from under the parent titlebar, moving with it, modal to it only. Needs WM cooperation on stacking and geometry constraints. Alternatively handle entirely client-side via an override-redirect child; that's simpler and I'd start there.
7. **ExposÃ© / Mission Control** â€” this is where the compositor fork earns its keep. xfwm4's compositor already holds named pixmaps for every window; a scaled-layout overlay mode is a bounded, high-visibility feature.

---

## 3. The global menubar

This is the hardest and most important piece. macOS apps *require* a screen-level menubar; without it, nothing feels right and many apps are unusable.

### 3.1 The two existing Linux mechanisms

**(a) DBusMenu + AppMenu Registrar** â€” the Unity/KDE lineage, and the de-facto standard.
- `com.canonical.dbusmenu` on the app side: `GetLayout(parentId, recursionDepth, propertyNames) â†’ (revision, layout)`, `AboutToShow(id) â†’ needUpdate`, `Event(id, eventId, data, timestamp)` where `eventId âˆˆ {clicked, hovered, opened, closed}`, plus `LayoutUpdated` / `ItemsPropertiesUpdated` signals.
- `com.canonical.AppMenu.Registrar` on the shell side: apps call `RegisterWindow(windowId, menuObjectPath)`; the shell calls `GetMenuForWindow(windowId)`. **Keyed by X11 window ID** â€” note that, it's a Â§4 argument.
- Reference implementation for XFCE: `vala-panel-appmenu` (has an `xfce4-appmenu-plugin`), which uses `unity-gtk-module` / `appmenu-gtk-module` to strip `GtkMenuBar`s out of GTK apps and re-export them.
- Qt supports DBusMenu natively. This is the widest-compatibility option.

**(b) `org.gtk.Menus` + `org.gtk.Actions`** â€” the GTK/GNOME native path.
- `GtkApplication` exports `GMenuModel`s on the bus and advertises them via X11 window properties `_GTK_UNIQUE_BUS_NAME`, `_GTK_MENUBAR_OBJECT_PATH`, `_GTK_APP_MENU_OBJECT_PATH`, `_GTK_APPLICATION_OBJECT_PATH`.
- Gated by XSettings keys `Gtk/ShellShowsMenubar` and `Gtk/ShellShowsAppMenu` â€” which `xfsettingsd` publishes, i.e. **your `openosxsettingsd` from Â§1.4 controls this switch.**
- Also X11-window-property-keyed.

### 3.2 Why neither is sufficient for NSMenu â€” and what to do about it

`NSMenu` carries state that DBusMenu simply cannot represent:

- `NSAttributedString` item titles (mixed fonts/colours)
- **Custom item views** (`NSMenuItem.view`) â€” arbitrary AppKit views inside menus, used constantly by real apps
- Key equivalents with full `NSEventModifierFlags`, including the Command/Option distinction and `keyEquivalentModifierMask`
- Lazy population via `NSMenuDelegate` (`menuNeedsUpdate:`, `numberOfItemsInMenu:`, `menu:updateItem:atIndex:shouldCancel:`)
- Automatic enablement via `NSMenuValidation` / `validateMenuItem:` walking the responder chain **at menu-open time**
- The Services menu, the standard app-menu items, `NSMenu.autoenablesItems`
- Mixed-state (dash) items, alternate items revealed by Option

**Recommendation â€” three-lane design:**

1. **Native lane (primary).** Define OpenOSX's own menu protocol over **Mach IPC / XPC**, not D-Bus. XPC is already in tree, it's the Darwin-native transport, and it's the same channel WindowServer already needs for window and event traffic. Model it on the real thing: the menubar process asks the frontmost app for its menu *lazily*, the app runs `NSMenuDelegate` and `validateMenuItem:` in its own address space at open time, and returns a render-ready tree (including serialised custom-view surfaces where present). Only this lane can be faithful.
2. **Export lane.** Publish the same menus as `com.canonical.dbusmenu` too. Costs little, and buys you: interop with anything expecting the standard, easy debugging with existing tools, and a migration path if you ever want a third-party bar.
3. **Import lane.** The menubar process also *implements* `com.canonical.AppMenu.Registrar` and reads `_GTK_MENUBAR_OBJECT_PATH`, so cross-built GTK and Qt apps in the image get their menus in the same bar. Set `Gtk/ShellShowsMenubar=1` via `openosxsettingsd`. **This is what makes it feel like one OS** rather than "macOS apps up top, GTK apps with their own menubars".

### 3.3 Where the menubar lives: panel plugin vs own process

- **Short term:** implement it as an **xfce4-panel external plugin** (`XfcePanelPlugin`, embedded via GtkPlug/GtkSocket). Fastest route to something on screen, reuses panel positioning/struts/autohide.
- **Long term: its own process, owned by you.** Reasons: (a) the menubar must be present before/independent of the panel; (b) it needs to own global keyboard grabs for key equivalents; (c) it must render with AppKit's own text/vibrancy, not GTK; (d) **on Wayland, external panel plugins cannot use GtkSocket/GtkPlug at all** â€” the Xfce Wayland roadmap lists this as an open blocker with a layer-shell + D-Bus workaround. Don't build your most identity-critical surface on a mechanism upstream has already flagged as dead.

Plan for the menubar strip to host **both** halves:
- **Left:** Apple-menu-equivalent (system menu) â†’ bold app name â†’ the frontmost app's `NSMenu`.
- **Right:** `NSStatusItem` menu-bar extras, clock, etc. Also implement `org.kde.StatusNotifierWatcher` (SNI) here so foreign apps' tray icons land in the same strip instead of a separate systray.

### 3.4 The focus-tracking problem

The menubar must follow the **active application**, and macOS keeps an app active with zero windows open. X11 gives you `_NET_ACTIVE_WINDOW` and `_NET_WM_PID`, which is window-level and PID-level â€” neither is app-level, and neither survives "last window closed".

**Do not derive menubar state from the window manager.** WindowServer knows which app is frontmost because AppKit tells it (`NSApplication activateIgnoringOtherApps:`, `NSRunningApplication`). Make WindowServer the authority: WM focus changes are an *input* to WindowServer's activation model, and the menubar subscribes to WindowServer, not to X11. This is another reason the menubar should be your process, not a panel plugin.

---

## 4. X11 vs Wayland for the compat layer

### Recommendation: **X11 (Xorg) is the primary target through 1.0.** Build the Wayland path behind a backend abstraction, and plan for OpenOSX's *own* wlroots-based compositor as the long-term destination â€” not sway, and not xfwl4.

### 4.1 Why X11 first

**Absolute global coordinates.** `NSWindow -setFrameOrigin:`, `-convertRectToScreen:`, `NSScreen.frame`, `CGDisplayBounds`, window cascading, saved frame autosave â€” all assume one flat global coordinate space where a client can position itself. **Wayland deliberately denies this.** This is not a gap that gets filled; it's a design position. It is the single thing that has held back Wine's Wayland driver for years â€” Wine has to resort to tricks for transient windows precisely because Win32 (like AppKit) assumes absolute positioning. You would be re-fighting that exact war, in the exact same shape.

**Window levels and stacking.** `NSWindowLevel` requires clients to influence z-order. Wayland gives clients no z-order control; `wlr-layer-shell` exists for *shell* components, not apps. Every floating palette, utility panel, and status window becomes a private-protocol problem.

**Menus and popups.** `NSMenu` popups are override-redirect windows at arbitrary global coordinates holding a pointer grab, dismissed on the client's terms. X11: trivial and battle-tested. Wayland: `xdg_popup` is parent-relative with compositor-controlled constraint/dismissal semantics that don't match AppKit's.

**Global input.** `CGEventTap`, `NSEvent +addGlobalMonitorForEvents:`, `CGWarpMouseCursorPosition`, `CGGetLastMouseDelta`, hot corners, system-wide hotkeys. X11: XTest, XI2, `XGrabPointer`, `XGrabKey`. Wayland: global event taps are *impossible by design*; `pointer-constraints` and `relative-pointer` are in-tree and help, and `pointer-warp-v1` exists (I see the enum header in `src/ThirdParty/wayland-protocols/`) but is very new.

**Screen capture.** `CGWindowListCreateImage`, `CGDisplayCreateImage`, and everything screenshot/screen-recording. X11: XComposite + XDamage + XShm, already working with the compositor you build. Wayland: needs `wlr-screencopy` / `ext-image-copy-capture`, and the Xfce Wayland roadmap explicitly records that **there is currently no Wayland protocol supporting active-window screenshots** â€” a gap that directly breaks a documented CoreGraphics API.

**Foreign surface embedding.** `XEmbed` / `XReparentWindow` lets WindowServer reparent and compose app windows, and lets a legacy GTK app be wrapped in Aqua chrome. Wayland has **no in-protocol embedding**; `libwlembed` is experimental and the Xfce roadmap lists it as the blocker holding up `xfce4-screensaver`. For a compat layer, embedding is not a nice-to-have.

**Ecosystem reality in your tree.** Mesa/GLX/EGL already works on X11 here, Xorg 21.1.24 is a real patched build (`patches/xorg-mitshm-openosx.patch`, `xorg-xvfb-*`), and PDGOP/PDVirglShim are wired for it. The Wayland session's compositor is **sway** â€” an i3-style *tiling* WM whose entire interaction model is hostile to Aqua floating windows. You would have to replace it regardless, so the Wayland path is not actually "already done."

**Upstream risk.** As of Jan 2026, Xfce's Wayland answer is **xfwl4** â€” a brand-new compositor written **from scratch in Rust on Smithay**, funded by Xfce and led by Brian Tarricone, explicitly *not* based on xfwm4. The earlier wlroots-in-xfwm4 work (merged May 2025) was abandoned as the wrong path; **xfwm4 stays X11-only**. Two consequences:
- Betting the compat layer on Xfce's Wayland future means chasing an unreleased Rust project and adding a **Rust cross-toolchain targeting Darwin** to a tree that is currently entirely C / Meson / Autotools. That is a large, unrelated infrastructure project.
- Conversely, since xfwm4 will never diverge into Wayland upstream, **forking xfwm4 costs you nothing in future merge pain** â€” its upstream is now effectively in maintenance for X11. That makes the hard fork in Â§1.2 much cheaper than it would have been a year ago.

### 4.2 The honest counterargument, and how to keep the door open

Every Wayland objection above assumes you are a *guest* on someone else's compositor. **You will not be.** Once the compositor is yours, you can define private protocols â€” `openosx_window_v1` with absolute positioning, levels, taps, and capture â€” and every objection evaporates. Wayland's restrictions are policy, not physics, and policy is yours when you write the compositor.

So the correct read is not "X11 good, Wayland bad." It's: **X11 lets you ship the compat layer *before* you have written a compositor. Wayland requires you to write one first.** Sequence accordingly.

To avoid painting into a corner:

1. **Define a `WindowBackend` abstraction inside WindowServer from commit one.** X11 impl first, Wayland impl second, exactly as `libxfce4windowing` does for XFCE (worth reading as a model even where you don't link it).
2. **Put `IOSurface` at the client boundary immediately.** Back it with DRI3/dmabuf. This is what `PDSurface` is already gesturing at. Then the phase-1 model (each `NSWindow` is a real X11 window; AppKit renders into it directly via Mesa; WindowServer does policy, menus, and events) and the phase-3 model (WindowServer owns all pixels and composites, true to macOS) share **one client-facing API**. Apps never notice the switch. This is the single most important forward-compatibility decision in the whole plan.
3. **Never let AppKit talk X11 directly.** All windowing goes AppKit â†’ WindowServer (XPC/Mach) â†’ backend. If any AppKit code calls Xlib, the Wayland migration becomes a rewrite.

### 4.3 Target architecture

```
   AppKit app (Mach-O)            AppKit app            GTK/Qt app (cross-built)
        â”‚  XPC/Mach                    â”‚                        â”‚
        â”‚  (windows, events,           â”‚                        â”‚
        â”‚   menus, IOSurface)          â”‚                        â”‚
        â–¼                              â–¼                        â”‚
  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”                 â”‚
  â”‚  WindowServer          (APSL/BSD, yours)  â”‚                 â”‚
  â”‚  Â· app activation model (authoritative)   â”‚                 â”‚
  â”‚  Â· NSWindow â†” toplevel mapping            â”‚                 â”‚
  â”‚  Â· event routing, taps, hotkeys           â”‚                 â”‚
  â”‚  Â· display/NSScreen model                 â”‚                 â”‚
  â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ WindowBackend â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â” â”‚                 â”‚
  â”‚  â”‚  X11Backend (phase 1)                â”‚ â”‚                 â”‚
  â”‚  â”‚  WaylandBackend (phase 3)            â”‚ â”‚                 â”‚
  â””â”€â”€â”´â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”´â”€â”˜                 â”‚
                        â”‚ X11 wire protocol                     â”‚
                        â–¼                                       â–¼
             â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
             â”‚  Xorg 21.1.24  +  forked xfwm4 (GPLv2, separate    â”‚
             â”‚  processes â€” wire protocol boundary only)          â”‚
             â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜

  Menubar process (yours) â”€â”€ XPC â”€â”€â–¶ WindowServer   (native lane)
         â”‚ â””â”€â”€ com.canonical.dbusmenu export        (export lane)
         â””â”€â”€â”€â”€ AppMenu.Registrar + _GTK_MENUBAR_OBJECT_PATH + SNI  (import lane)

  openosxsettingsd (yours) â”€ implements org.xfce.Xfconf + XSettings over CFPreferences
  launchd â”€ owns session startup (replaces xfce4-session)
```

Note the GPL boundary: it is exactly the X11 socket. Nothing OpenOSX authors links into a GPL process, and nothing GPL links into AppKit.

**Long-term compositor:** when you do move, build **OpenOSX's own wlroots-based compositor that *is* the WindowServer**, with XWayland for legacy. wlroots is MIT, already in tree, C (fits the existing cross-build), and merging the compositor into WindowServer eliminates a process hop and the entire level/positioning/tap negotiation. Do **not** fork xfwl4 (Rust/Smithay â€” new toolchain, unreleased, and its design goal is "feel exactly like xfwm4", which is not your design goal). Do track its private protocol designs for ideas.

**Phasing:**

| Phase | Deliverable |
|---|---|
| 0 | Subtree + rebrand the six forked components; `LICENSING.md`; X11 session boots branded |
| 1 | `openosxsettingsd` (xfconf iface over CFPreferences); launchd session; retire xfce4-session/xfconfd/garcon; OpenOSX SSD theme for Path B |
| 2 | WindowServer with `X11Backend` + `IOSurface`-over-dmabuf; AppKit CSD path; xfwm4 fork changes (levels, shadow suppression, Cmd un-grab) |
| 3 | Menubar process: native lane + export lane + import lane + SNI; app-level activation & `Cmd-Tab` |
| 4 | WindowServer absorbs the WM role; ExposÃ©/Mission Control in the compositor |
| 5 | `WaylandBackend` + own wlroots compositor + XWayland; private `openosx_window_v1` protocol |

---

## 5. Identity and branding

### 5.1 The thesis

"XFCE with a skin" is what you get if you fork the *look*. "Its own OS" is what you get if you fork the look **and replace the plumbing with the Darwin-native equivalents you already have.** Â§1.4 is the branding strategy as much as the engineering one â€” a user who runs `defaults write` and sees the window manager change behaviour is in a different operating system, not a theme.

Second thesis: **the global menubar is the identity.** It is the single most recognisable structural difference between this and every other Linux desktop, and it's also a hard requirement for the app-compat goal. Prioritise it above wallpapers, icons, and traffic-light pixel-perfection.

### 5.2 Naming

Two constraints to respect simultaneously:

- **Xfce marks** â€” don't use the name or logo in branding; rename binaries, D-Bus names (`org.xfce.*` â†’ `org.openosx.*`), config channels, `.desktop` files, icon theme names, session names. Ship **alias bus names** for `org.xfce.*` so third-party plugins keep working.
- **Apple marks** â€” this is the subtler one. *API* identifiers (`AppKit`, `NSWindow`, `CoreGraphics`) are functional interface names you must keep for compatibility; that's the Wine/ReactOS posture and it's defensible. But **product-facing** names are different. **Aqua, Finder, Dock, Spotlight, Quartz, Cocoa, Mission Control are Apple trademarks.** Use them internally as shorthand if you like; do not ship them as the names of OpenOSX's user-visible components or design language.

**Proposed naming family â€” GalÃ¡pagos** (ties to Darwin heritage, unmistakably not-Apple, memorable, and gives you a deep well of names):

| Role | XFCE origin | Proposed name |
|---|---|---|
| The desktop environment | â€” | **GalÃ¡pagos** (or just "the OpenOSX Desktop") |
| Compositor / window manager | xfwm4 | **Iguana** â€” or, more soberly, `openosx-windowserver` |
| Menu bar | xfce4-panel | **Ridge** |
| Dock | (new) | **Shelf** |
| Desktop layer | xfdesktop | **Canvas** |
| File manager | thunar | **Finch** |
| App launcher / search | xfce4-appfinder | **Beacon** |
| Settings daemon | xfconfd | `openosxsettingsd` |
| Design language | â€” | **Lumen** |

A more restrained alternative if the animal names feel twee: an optics family â€” **Prism** (compositor), **Facet** (menu bar), **Lumen** (design language), **Halo** (accent/vibrancy system).

Either way, name the **design language** explicitly and separately from the DE. Having a named, documented visual language is what separates a platform from a theme.

### 5.3 A real HIG, not a theme file

Write and publish an actual `OpenOSX Human Interface Guidelines` document early â€” even a thin one. It's cheap, it forces the decisions below to be made once rather than per-component, and it's a strong contributor-recruitment artifact.

**Metrics.** Base unit 4pt. Standard control heights 22 / 28 / 32pt. Corner radii: 6pt controls, 10pt windows, 6pt menus. Window titlebar 28pt standard / 52pt unified-with-toolbar. Menu bar 24pt. These are the numbers apps' auto-layout will implicitly assume; pick them deliberately and put them in a token file consumed by *both* AppKit and the GTK theme.

**Typography â€” this needs solving early, and there's a trap.** San Francisco is proprietary and cannot ship. Recommendations:
- System font â†’ **Inter** (SIL OFL). Register it as the resolution for `NSFont systemFontOfSize:` and for `.SFNSText` / `.AppleSystemUIFont`.
- Monospace â†’ **JetBrains Mono** (OFL) or **IBM Plex Mono** (OFL).
- **Ship a fontconfig alias table** mapping Apple font names that apps hardcode: `Helvetica` / `Helvetica Neue` â†’ **TeX Gyre Heros** (GUST licence, permissive â€” prefer over URW Nimbus Sans, which is AGPL+font-exception and messier); `Lucida Grande` â†’ Inter; `Menlo` / `Monaco` â†’ JetBrains Mono. Without this table, a large fraction of real apps render with fallback boxes and look broken for reasons that have nothing to do with AppKit.

**Colour â€” make this do double duty.** Define a semantic token set whose names *are* the `NSColor` semantic names: `labelColor`, `secondaryLabelColor`, `separatorColor`, `controlAccentColor`, `controlBackgroundColor`, `selectedContentBackgroundColor`, `windowBackgroundColor`, `underPageBackgroundColor`. Then:
- AppKit's `NSColor` semantic accessors resolve straight from the theme â€” compat for free.
- The GTK3 theme, the xfwm4 theme, and the icon theme all consume the same tokens â€” coherence for free.
- Light/Dark (`NSAppearance`) and a user accent colour are **structural from day one**, plumbed through `openosxsettingsd` so the whole desktop â€” AppKit apps, GTK apps, window frames â€” switches together in one transaction. A desktop where the theme switch is atomic across toolkits is immediately, visibly better than XFCE.

**Deliberate divergences.** Pick a handful of places where you are *not* copying macOS, so the result reads as its own platform and not a knockoff. Suggestions:
1. **Window controls:** keep left placement and three-button semantics (functional, needed for muscle memory *and* app expectations), but use your own glyph set and a single-hue or accent-derived colour ramp rather than Apple's exact red/amber/green circles. This is both a design choice and the prudent trade-dress position.
2. **Menu bar never auto-hides**, including in fullscreen â€” a small, opinionated, defensible difference.
3. **First-class Sessions** â€” named, restorable window/workspace sets, surfaced in the system menu. Wayland's `xdg-session-management` (on xfwl4's roadmap) and X11 both admit this; nobody does it well; it's a genuine feature rather than a reskin.
4. **The Shelf is not the Dock** â€” pick a different organising principle (e.g. recency + pinned + running as three visually distinct zones) rather than reproducing Dock behaviour.

**Rebranding mechanics.** The repo already uses `git subtree` (per the `kc-tools` / `xnu-loader` commits) â€” use exactly that workflow for each forked Xfce component under `src/ThirdParty/`, so upstream rebases stay mechanical. Per-component checklist: rename binaries and `.desktop` files; rename D-Bus names with `org.xfce.*` aliases retained; rename xfconf channels with a migration shim; swap icon theme name; add "modified by the OpenOSX project" notices to changed files (GPLv2 Â§2(a)); keep `COPYING`/`COPYING.LIB`; add the component to the release source-tarball manifest.

---

## Summary of the five recommendations

1. **Fork the pixel-pushers (xfwm4, xfce4-panel, xfdesktop), replace the plumbing (xfce4-sessionâ†’launchd, xfconfdâ†’CFPreferences daemon, garconâ†’LaunchServices), track the apps.** Licensing: GPLv2 rebranding is fine; the real rule is that OpenOSX-authored code never *links* GPL code â€” only wire protocols cross that boundary. Cocotron (MIT) is the safe reference to vendor; GNUstep (LGPLv2.1) at arm's length; Darling (GPL) study only.
2. **AppKit draws its own Aqua-style chrome client-side** â€” as real macOS does â€” because `themerc`'s pixmap engine cannot express unified titlebars, sheets, or vibrancy, and because CSD keeps the chrome out of GPL code. Ship a first-party xfwm4 pixmap theme (`button_layout=CHM|`, alpha corners, prelight glyphs) only for non-AppKit apps.
3. **Menubar: own process, three lanes** â€” native XPC protocol for `NSMenu` fidelity (custom views, lazy delegates, `validateMenuItem:`), `com.canonical.dbusmenu` export for interop, `AppMenu.Registrar` + `_GTK_MENUBAR_OBJECT_PATH` + SNI import so GTK/Qt apps land in the same bar. Drive activation from WindowServer's app model, not from X11 focus.
4. **X11 primary through 1.0**, behind a `WindowBackend` abstraction, with `IOSurface`-over-dmabuf at the client boundary from day one. Wayland denies exactly the things AppKit needs most (absolute positioning, z-order, global taps, window capture, embedding) *when you are a guest* â€” and Xfce's Wayland future is xfwl4, an unreleased from-scratch Rust/Smithay compositor. Long term: OpenOSX's own wlroots compositor that *is* WindowServer.
5. **Identity comes from the plumbing plus the menubar plus a named design language** â€” a settings system unified with `defaults`, an atomic cross-toolkit light/dark/accent switch, a real published HIG with OFL fonts and `NSColor`-named semantic tokens, and a few deliberate non-macOS divergences so it reads as its own platform.

## Sources

- [Xfce component license audit](https://wiki.xfce.org/licenses/audit)
- [Xfwm4 Window Component and Theming how-to](https://wiki.xfce.org/howto/xfwm4_theme)
- [Xfce Wayland roadmap](https://wiki.xfce.org/releng/wayland_roadmap)
- [Xfwm4 Being Developed As New Wayland Compositor For Xfce (xfwl4) â€” Phoronix](https://www.phoronix.com/news/Xfce-Xfwl4-Wayland)
- [Xfwl4: the roadmap for a Xfce Wayland compositor â€” LWN](https://lwn.net/Articles/1056159/)
- [Xfwl4 â€“ The roadmap for a Xfce Wayland Compositor](https://alexxcons.github.io/blogpost_15.html)
- [Xfce's xfwm4 Merges Wayland Compositor Code Based On wlroots â€” Phoronix](https://www.phoronix.com/news/Xfce-xfwm4-Merges-Wayland-Code)
- [Xfce 4.20 tour](https://www.xfce.org/about/tour420)
- [libxfce4panel Reference Manual â€” Panel Plugins](https://developer.xfce.org/xfce4-panel/libxfce4panel-plugins.html)
- [Xfce panel plugin howto](https://wiki.xfce.org/dev/howto/panel_plugins)
- [vala-panel-appmenu (global menu for xfce4-panel)](https://github.com/rilian-la-te/vala-panel-appmenu)
- [com.canonical.AppMenu.Registrar interface XML](https://github.com/rilian-la-te/vala-panel-appmenu/blob/master/subprojects/registrar/data/com.canonical.AppMenu.Registrar.xml)
- [com.canonical.dbusmenu interface XML (KDE plasma-workspace)](https://invent.kde.org/valdikss/plasma-workspace/-/blob/work/krunner_retain_prior_search/libdbusmenuqt/com.canonical.dbusmenu.xml)
- [GApplication D-Bus API (`_GTK_MENUBAR_OBJECT_PATH`, `_GTK_UNIQUE_BUS_NAME`)](https://wiki.gnome.org/Projects/GLib/GApplication/DBusAPI)
- [appmenu-gtk-module](https://github.com/paysonwallach/appmenu/blob/master/subprojects/appmenu-gtk-module/README.md)
- [A Wayland driver for Wine â€” Collabora](https://www.collabora.com/news-and-blog/news-and-events/a-wayland-driver-for-wine.html)
- [What to Expect From Wine on Wayland in 2024 â€” GamingOnLinux](https://www.gamingonlinux.com/2024/01/what-to-expect-from-wine-on-wayland-in-2024/)
- [xfwm4 compositor source](https://github.com/xfce-mirror/xfwm4/blob/master/src/compositor.c)
- [elcapitan-xfwm (macOS-style xfwm4 theme)](https://github.com/micopiira/elcapitan-xfwm)
- [WhiteSur GTK theme](https://github.com/vinceliuice/WhiteSur-gtk-theme)
- [GNUstep gui library (LGPLv2.1)](https://github.com/gnustep/libs-gui)
- [Cocotron / Cocoa environments overview](https://apple-developer.org/resources/environments.html)

