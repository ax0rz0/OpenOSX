# OpenOSX GUI test corpus

The three applications chosen as the milestone ladder for the macOS
compatibility work. Each is a real, unmodified macOS `.app` — we do not
recompile them for OpenOSX; making the shipped Mach-O binary run *is* the test.

See [MACOS_COMPAT.md](MACOS_COMPAT.md) for the measurement methodology
(symbol / class / selector coverage) and [AQUA_UI_PLAN.md](AQUA_UI_PLAN.md)
for the stack that has to exist underneath them.

## Hard prerequisite for all three

The x86_64 slice with `minos` ≤ 11.0. Darwin 20.5 reports macOS 11.4, and dyld
refuses any binary whose minimum deployment target is newer than the running
system — so "the newest release" is usually the wrong download. Prefer the last
release that still supported Big Sur, and always verify before filing a bug:

```sh
lipo -archs /Applications/Foo.app/Contents/MacOS/Foo    # expect x86_64 present
otool -l  /Applications/Foo.app/Contents/MacOS/Foo | grep -A3 LC_BUILD_VERSION
```

## Redistribution rule

**Only the freely-redistributable apps may ever be committed or shipped in a
published image.** The Powder Toy and VLC are open source and fine to keep in a
local test image; Safari is Apple proprietary and must exist **only in the
user's own local image on their own machine** — never committed to the repo,
never included in a release artifact, never mirrored anywhere. This is a hard
rule, not a preference.

## Tier 1 — The Powder Toy (first GUI target)

FOSS (GPL-3.0), ships an official x86_64 macOS build.

The best possible early target, because it is an **SDL2** application: SDL2's
macOS backend uses a deliberately narrow slice of Cocoa — `NSApplication` and
its run loop, one `NSWindow`, one drawing view, keyboard/mouse event delivery,
and a pixel buffer or GL context. There is no document architecture, no
toolbars, no bindings, no storyboards. That is close to the minimum viable
AppKit surface described in AQUA_UI_PLAN.md phase G3, which makes it a
*measurable* first goal rather than an open-ended one.

Second advantage: SDL2 is open source, so when something fails we can read
exactly which API the app called and why it expected what it expected —
debugging against a known-good reference rather than a black box.

Success criterion: the window opens, the simulation renders, and mouse input
draws particles.

## Tier 2 — VLC media player

FOSS (GPL-2.0+), ships official Intel macOS builds.

Substantially harder than Tier 1 and a genuine test of breadth. Its Cocoa
interface is hand-written Objective-C with many custom `NSView`/`NSWindow`
subclasses, menus, panels and controls — so it exercises the parts of AppKit
that Tier 1 skips entirely, including the `__objc_superrefs` subclassing
surface that requires ivar-layout compatibility rather than merely correct
selectors. Beyond AppKit it wants media frameworks OpenOSX does not have yet
(CoreAudio for output, VideoToolbox/AVFoundation paths for decode
acceleration), though software decode with a basic audio sink is a plausible
first milestone.

Staged success criteria: (1) launches and shows its UI, (2) opens a file
dialog, (3) decodes and displays video with software decoding, (4) audio.

## Tier 3 — Safari (end goal, local only)

Apple proprietary. The stated end goal, and correctly treated as years out
rather than a next step.

Safari is not a normal application: it is a multi-process system component.
It depends on Apple's own `WebKit.framework` (a different thing from the
WebKitGTK we cross-build — shared engine lineage, entirely different API and
process model), on XPC services for its WebContent and Networking processes,
on CoreAnimation for compositing, and on sandbox and system entitlements that
assume a code-signing world OpenOSX does not enforce. Each of those is a
separate multi-month project.

Target the last Big Sur-compatible Safari release, and expect the honest first
result to be a launch failure with a long list of missing symbols — which is
still useful, because that list is exactly the roadmap.

## What to do with each app before writing any code

Run the coverage tooling (MACOS_COMPAT.md §6) against all three and commit the
reports. The output tells us, per app: which dylibs are missing outright (hard
blockers), which classes it subclasses (the expensive work), and which
selectors it sends (the long tail). That converts "make Mac apps run" into a
ranked, finite work list — and it can be done today, before AppKit exists,
because it only needs the binaries and `otool`/`nm`.
