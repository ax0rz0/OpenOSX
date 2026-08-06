# OpenOSX Graphics Foundation: CoreGraphics / Quartz 2D

**Research report â€” clean-room implementation strategy**

---

## Executive summary (read this if nothing else)

Four findings change the shape of this project:

1. **There is already a permissively-licensed, Quartz-shaped, working CoreGraphics implementation you can vendor today.** [ravynOS](https://github.com/ravynsoft/ravynos) ships `Frameworks/CoreGraphics` as a *thin forwarding shim* over `Frameworks/Onyx2D`, both descended from Cocotron. I pulled the actual header of [`ravynos/Frameworks/CoreGraphics/CGContext.m`](https://github.com/ravynsoft/ravynos/blob/main/Frameworks/CoreGraphics/CGContext.m) â€” it is verbatim MIT ("Copyright (c) 2006-2007 Christopher J. W. Lloydâ€¦ Permission is hereby granted, free of chargeâ€¦"). The repo overall is BSD-2-Clause. **This is license-ideal for an APSL/BSD image and requires zero Apple source contact.**

2. **The correct architecture is already proven: `CoreGraphics.framework` = ABI shim â†’ internal renderer object model â†’ pluggable rasterizer backend.** Cocotron/ravynOS call the middle layer `O2*` (Onyx2D); GNUstep calls it Opal; Darling forked Opal. All three landed on the same shape independently. Do not invent a fourth.

3. **OpenOSX is *ahead* of ravynOS on the part ravynOS is worst at.** ravynOS [0.5.0 "Sneaky Snek"](https://github.com/ravynsoft/ravynos/releases/tag/v0.5.0) *removed* Wayland and DRM from its WindowServer and now renders unaccelerated directly to the BSD EFI framebuffer. OpenOSX already ships Mesa, wlroots, X11, libdrm/gbm and PDSurface. **The natural split: take ravynOS's framework layer, keep OpenOSX's compositor layer.** That is a real, non-obvious synergy.

4. **Binary compatibility is measurable, not guessable.** The macOS SDK ships `CoreGraphics.tbd` â€” a TAPI text-based stub that is a machine-readable list of *every* exported symbol, plus install-name and compatibility-version. That is your target spec, it is legal to read (it is an interface list, and you already legally build against these SDKs), and it is diffable in CI. **Build the coverage harness before writing a single drawing primitive.**

---

## 1. What CoreGraphics/Quartz actually is, from an app's perspective

### 1.1 The drawing model

Quartz 2D is a **PDF-imaging-model** rasterizer. This is the single most important architectural fact, and it drives every backend decision below. Per Apple's [Quartz 2D Programming Guide](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/drawingwithquartz2d/dq_overview/dq_overview.html), Quartz uses the **painter's model**: "the shape drawn first can be overlayed by a solid shape, which obscures all but the perimeter of the first shape." There is no retained scene graph, no z-order, no dirty-region reasoning inside CG. Calls mutate state and emit marks, immediately, in order.

The consequence: CG's semantics are *the PDF 1.4 imaging model*. Its blend modes are the PDF blend modes. Its color spaces are ICC/PDF color spaces. Its shading functions are PDF type-0/2/3 functions. **This is why Cairo maps to CG almost 1:1 and why Skia does not** â€” Cairo was designed against the same PostScript/PDF lineage.

### 1.2 CGContext â€” the central object

Per the [Graphics Contexts chapter](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/drawingwithquartz2d/dq_context/dq_context.html), `CGContextRef` is "an opaque data typeâ€¦ that encapsulates the information Quartz uses to draw images to an output device," carrying colors, stroke/fill mode, line width and style, font info, and compositing options.

The state machine is:

- **A graphics state stack** â€” `CGContextSaveGState` / `CGContextRestoreGState`. Everything below is per-state and stacked.
- **A CTM** (current transformation matrix) â€” `Scale/Translate/Rotate/ConcatCTM`, `GetCTM`.
- **A current path** â€” mutable, built with `MoveToPoint/AddLineToPoint/AddCurveToPoint/AddQuadCurveToPoint/AddArc/AddArcToPoint/AddRect/AddEllipseInRect/AddPath/ClosePath`, then *consumed* by a paint operation (`FillPath`, `EOFillPath`, `StrokePath`, `DrawPath`) or by `Clip`/`EOClip`.
- **A clip region** â€” intersective only; it can never be widened except by `RestoreGState`. `ClipToMask` accepts an 8-bit mask image. This "clip only narrows" invariant is load-bearing for correctness.
- **Paint sources** â€” solid color (`SetFillColorWithColor`, plus the RGB/Gray/CMYK convenience setters), patterns (`SetFillPattern` + `SetPatternPhase`), gradients (`DrawLinearGradient`/`DrawRadialGradient`), and arbitrary-function shadings (`DrawShading`).
- **Compositing controls** â€” `SetAlpha`, `SetBlendMode`, `BeginTransparencyLayer`/`EndTransparencyLayer`, `SetShadowWithColor`.
- **Text state** â€” text matrix (separate from CTM), text position, character spacing, drawing mode (fill/stroke/clip/invisible), `SetFont`/`SetFontSize`/`ShowGlyphsAtPositions`.

I enumerated the full public function list from the SDK header ([`CGContext.h`, "Copyright (c) 2000-2012 Apple Inc."](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.9.sdk/System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers/CGContext.h)) â€” roughly 130 exported functions in `CGContext.h` alone, grouped as: gstate (2), CTM (5), drawing attributes (8), path construction (14), path query (5), path drawing (12), clipping (6), color (17), images (4), shadow (2), gradient/shading (3), text (9 current + 6 deprecated), PDF (1+1 deprecated), pages (2), lifecycle (5), antialiasing (2), font smoothing (6), transparency layers (3), userâ†”device space (7).

### 1.3 The other type families

| Header | What it defines | Notes for us |
|---|---|---|
| `CGGeometry.h` | `CGPoint`, `CGSize`, `CGVector`, `CGRect` + ~45 functions | Pure math. Zero backend. Implement first. |
| `CGAffineTransform.h` | `CGAffineTransform` (a,b,c,d,tx,ty) | Pure math. |
| `CGColorSpace.h` | Device/Generic/ICC/Indexed/Pattern spaces | The hard part is real ICC. `lcms2` (MIT) solves it. |
| `CGColor.h` | Color = colorspace + component array | CFType. |
| `CGPath.h` | Immutable/mutable path, `CGPathApply` | Pure math + flattening. |
| `CGImage.h` | Pixel buffer + colorspace + `CGBitmapInfo` (alpha layout + byte order) | See Â§1.5. |
| `CGDataProvider.h` / `CGDataConsumer.h` | Callback-based I/O | Trivial, but *many* apps use them. |
| `CGFont.h` | Font handle, glyph advances, glyph paths | FreeType backs this. |
| `CGGradient.h`, `CGShading.h`, `CGFunction.h`, `CGPattern.h` | Paint sources | `CGFunction` is an arbitrary C callback evaluator â€” see Â§3.3. |
| `CGLayer.h` | Offscreen, context-compatible, reusable drawing buffer | Easy and high-value (AppKit uses it). |
| `CGPDF*.h` (12 headers) | Full PDF parser + generator | **Defer entirely.** Not needed to draw a window. |
| `CGError.h`, `CGBase.h` | Errors, `CGFloat`, export macros | Trivial. |

The umbrella [`CoreGraphics.h`](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.8.sdk/System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers/CoreGraphics.h) includes: CGColorSpace, CGContext, CGDataConsumer, CGDataProvider, CGError, CGFont, CGFunction, CGGeometry, CGGradient, CGImage, CGLayer, CGPath, CGPattern, CGShading, and the CGPDF* family.

### 1.4 Coordinate system â€” the classic footgun

Quartz user space is **y-up, origin lower-left**: "The origin of the user coordinate space is the point (0,0), located at the lower-left corner of the pageâ€¦ the y-axis increases in value as it moves from the bottom toward the top." Cairo, Skia, X11, and Wayland are all **y-down, origin top-left**.

This is not a detail you paper over at the top. Bake a *base CTM* into every context at creation (`[1 0 0 -1 0 height]` for a device-space-down backend) and never think about it again. Getting this wrong late is a multi-week regression hunt, because it interacts with `CGContextConvertRectToDeviceSpace`, glyph rendering (text matrix vs CTM), shadow offsets (`CGContextSetShadow`'s offset is in *user* space and flips sign), and image drawing.

Also: `CGBitmapContextCreate` bitmaps are **top-row-first in memory** even though user space is y-up. So a bitmap context's base CTM is flipped relative to a PDF context's. Nail this in a unit test on day one.

### 1.5 CGBitmapInfo â€” the interop contract

`CGBitmapContextCreate(data, width, height, bitsPerComponent, bytesPerRow, colorspace, bitmapInfo)` is the API through which literally all window drawing will flow in your architecture. `CGBitmapInfo` encodes two orthogonal things: the **alpha layout** (`kCGImageAlphaPremultipliedFirst/Last`, `NoneSkipFirst/Last`, `Only`, `None`) and the **byte order** (`kCGBitmapByteOrder32Host/32Little/32Big`).

Critically: **Quartz stores premultiplied alpha.** So do Cairo (`CAIRO_FORMAT_ARGB32`), Skia (`kPremul_SkAlphaType`), Wayland `wl_shm` ARGB8888, and DRM `DRM_FORMAT_ARGB8888`. On little-endian x86_64, `kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host` is byte-for-byte identical to `CAIRO_FORMAT_ARGB32` and to `wl_shm` ARGB8888. **That means zero-copy from CG bitmap â†’ compositor surface.** Design around that one format as the fast path and convert everything else.

### 1.6 What is legally implementable

Everything in Â§1.2â€“Â§1.5 is declared in **public SDK headers** that OpenOSX already builds against, and described in Apple's **public developer documentation**. Function names, type names, struct layouts, and enum values are interface facts. Reimplementing them from a published interface specification is exactly the ReactOS/Wine/Mono/Cocotron/GNUstep model and is supported by *Google LLC v. Oracle America* (2021) on the API-reimplementation question.

**The private `CGS*` surface (Â§4.5) is a different legal category** and needs different handling â€” see below.

---

## 2. Existing open reimplementations, with licenses

### 2.1 Comparison table

| Project | License | Backend | Maturity | Verdict for OpenOSX |
|---|---|---|---|---|
| **ravynOS CoreGraphics + Onyx2D** | **MIT** (file headers) inside a **BSD-2-Clause** tree | Onyx2D built-in rasterizer + FreeType | Actively developed (0.5.x, 2025-26); ~60 files in `Frameworks/CoreGraphics` | â˜… **Vendor this.** Best license, closest architecture, actively maintained. |
| **Cocotron** ([cjwl/cocotron](https://github.com/cjwl/cocotron)) | **MIT** | Onyx2D built-in rasterizer; `O2Context_gdi` on Windows; `O2Font_freetype` | **Dead** (~8 years stale) | Upstream of the above. Read for reference; vendor from ravynOS instead. |
| **GNUstep Opal** ([gnustep/libs-opal](https://github.com/gnustep/libs-opal)) | **LGPL-2.1** | **Cairo** + freetype + fontconfig + lcms2 + libjpeg/png/tiff | Self-described: "not yet suitable for general useâ€¦ interesting for developers only" | Study the Cairoâ†”CG mapping â€” it is the best worked example. Vendoring makes CoreGraphics.framework LGPL (see Â§2.3). |
| **Darling CoreGraphics** ([darlinghq/darling-coregraphics](https://github.com/darlinghq/darling-coregraphics)) | **LGPL-2.1** (fork of Opal) | Cairo | Behind upstream Opal in places | **Study only.** No advantage over upstream Opal. |
| **Skia** ([google/skia](https://github.com/google/skia)) | **BSD-3-Clause** | CPU raster + Ganesh (GL/Vulkan/Metal) + Graphite | Production (Chrome, Android, Flutter, WebKitGTK) | Excellent *backend* candidate; not a CG API. |
| **Cairo** ([cairographics.org](https://www.cairographics.org/)) | **LGPL-2.1 OR MPL-1.1** (your choice) | image / GL / xlib / PDF / PS | Mature, **maintenance mode** | Excellent *backend*; already in your image. |

### 2.2 What Darling actually teaches us

Darling's own status is the cautionary tale. Per current reporting, Darling "based its AppKit and Foundation on the source of Cocotron, which was outdated when forked," GUI support remains limited to "simple graphical programs," and usability is "primarily console-based applications." Their [tracking issue #937 "Cocotron (AppKit) backend rework"](https://github.com/darlinghq/darling/issues/937) states the intended design directly: **AppKit should make `CGS*` calls to a window server, and CoreGraphics should provide backends implementing the CGS interfaces.**

That is the right layering, and Darling has been stuck on it for years â€” because they never built the compositor. **OpenOSX has the compositor already.** This is the single biggest asymmetry in your favor.

### 2.3 Licensing analysis â€” precise, not hand-wavy

- **MIT (Cocotron/Onyx2D/ravynOS frameworks)**: attribution only. Ships in an APSL/BSD image with no friction. **Preferred for anything inside `/System/Library/Frameworks`.**
- **BSD-3 (Skia)**: same practical story. Fine to statically link into your framework.
- **LGPL-2.1 (Opal)**: dynamically linking *against* an LGPL library from a proprietary/permissive app is explicitly permitted. But if `CoreGraphics.framework` is itself *derived from* Opal, then **that framework binary is LGPL** â€” you must offer its source and permit relinking. Since OpenOSX is open source and the framework is a separate `.dylib`, both conditions are trivially satisfied. So Opal is *shippable*, contrary to reflexive fear. It is simply worse than MIT: it constrains future relicensing, complicates redistribution by downstreams, and creates a viral boundary you have to explain forever. **Use Opal as a reference text, not as vendored code.**
- **Cairo (LGPL-2.1 OR MPL-1.1)**: elect **MPL-1.1**. MPL is file-level copyleft â€” modifications to Cairo's own files must be published, but nothing propagates into your framework. That sidesteps the LGPL relinking conversation entirely. **Document the election explicitly in your `LICENSES/` manifest.**
- **GPL anywhere in a shippable image component**: hard no, consistent with your existing policy.

**Clean-room hygiene, restated for the record:** every source above is a legitimate input. Public SDK headers, `.tbd` interface files, Apple's published developer documentation, observed runtime/ABI behavior of binaries, and compatibly-licensed open reimplementations are all fine. Leaked Apple source is not, ever, for anyone who will touch this code. Add a contributor attestation to `CONTRIBUTING.md` for the graphics subtree specifically â€” ReactOS's precedent is that the *taint claim* is as damaging as the taint.

---

## 3. Architecture recommendation

### 3.1 The three-layer stack

```
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚ Real Mach-O x86_64 app                                        â”‚
â”‚   LC_LOAD_DYLIB â†’ /System/Library/Frameworks/                 â”‚
â”‚                   CoreGraphics.framework/Versions/A/CoreGraphicsâ”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                                â”‚  strict ABI boundary
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚ LAYER 1 â€” CoreGraphics.framework  (C, thin, ABI-exact)        â”‚
â”‚   â€¢ CFRuntime-registered CFTypes: CGContext, CGImage, CGColor,â”‚
â”‚     CGColorSpace, CGPath, CGFont, CGGradient, CGPattern,      â”‚
â”‚     CGShading, CGLayer, CGDataProvider/Consumer               â”‚
â”‚   â€¢ Every public symbol from CoreGraphics.tbd, exported       â”‚
â”‚   â€¢ Pure forwarding â€” NO rasterization logic lives here       â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚ LAYER 2 â€” Onyx2D  (object model + semantics)                  â”‚
â”‚   O2Context (abstract) / O2Image / O2Path / O2ColorSpace â€¦    â”‚
â”‚   Owns: gstate stack, CTM, clip stack, PDF-model semantics,   â”‚
â”‚          blend-mode dispatch, colorspace conversion (lcms2)   â”‚
â”‚   Vendored MIT from ravynOS. Backend chosen by vtable.        â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
           â”‚                 â”‚                  â”‚
  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€â”€â”€â” â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€â”€â”€â”€â” â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
  â”‚ O2Context_    â”‚ â”‚ O2Context_     â”‚ â”‚ O2Context_       â”‚
  â”‚  builtin      â”‚ â”‚  cairo         â”‚ â”‚  skia   (later)  â”‚
  â”‚ (MIT, CPU,    â”‚ â”‚ (default:      â”‚ â”‚ (GPU: Ganeshâ†’GL) â”‚
  â”‚  no deps â€”    â”‚ â”‚  cairo-image / â”‚ â”‚                  â”‚
  â”‚  bring-up &   â”‚ â”‚  cairo-gl)     â”‚ â”‚                  â”‚
  â”‚  fallback)    â”‚ â”‚                â”‚ â”‚                  â”‚
  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜ â””â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”˜ â””â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                             â”‚                  â”‚
                    â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                    â”‚ OpenOSX existing stack:             â”‚
                    â”‚ Mesa (llvmpipe / virgl) Â· libdrm/gbmâ”‚
                    â”‚ FreeType Â· fontconfig Â· HarfBuzz    â”‚
                    â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                    â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â–¼â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                    â”‚ CGS layer â†’ OpenOSX WindowServer    â”‚
                    â”‚ (PDSurface / wl_shm) â†’ wlroots/xfwm4â”‚
                    â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

### 3.2 Why Cairo first, Skia later â€” and why not Skia first

**Cairo wins the correctness race.** Cairo's operator set (`CAIRO_OPERATOR_MULTIPLY` through `CAIRO_OPERATOR_HSL_LUMINOSITY`) is the PDF blend-mode set, which is *definitionally* `kCGBlendMode*`. Cairo's `cairo_pattern_t` + source/mask/clip model is the Quartz paint model. Cairo has native `cairo_push_group()` â†’ CG transparency layers. Opal exists as a 2,000-hour worked proof that the mapping closes. And **you already ship Cairo, FreeType, fontconfig, Pango and Mesa** â€” zero new build-system risk.

**Skia wins the performance race, later.** WebKitGTK/WPE replaced Cairo with Skia in [2.46 (Oct 2024)](https://webkitgtk.org/2024/10/04/webkitgtk-2.46.html) after [Igalia's evaluation](https://blogs.igalia.com/carlosgc/2024/02/19/webkit-switching-to-skia-for-2d-graphics-rendering/), reporting MotionMark gains "up to four times better on powerful desktops with discrete GPUs" and "doubled on low-end laptops using integrated GPUs" â€” and noting Cairo "is no longer receiving active development." That is a real and permanent signal about Cairo's future.

But Skia as a *first* backend is a trap for you specifically:
- **No stable API or ABI.** Skia's own [release notes](https://github.com/google/skia/blob/main/RELEASE_NOTES.md) document breaking signature changes as routine. You would be chasing a moving target while also chasing CG semantics.
- **C++-only, GN/Ninja build, vendored toolchain.** Cross-compiling that to Darwin/x86_64 against your SDK is a project in itself. WebKit sidestepped this by importing Skia under `Source/ThirdParty`.
- **`SkColorSpace` is narrower than `CGColorSpace`.** Skia assumes a small set of well-behaved spaces; CG assumes arbitrary ICC profiles, Indexed spaces, and Pattern spaces. You would be fighting the abstraction.

**Recommendation: three backends behind one vtable, in this order.**
1. `O2Context_builtin` â€” already written, MIT, zero dependencies. Use it for **bring-up and CI golden-image tests**, and keep it forever as the no-GPU fallback (it is what will render when a VM has no virgl).
2. `O2Context_cairo` â€” the **default shipping backend**. Correctness-first. `cairo-image` on CPU, `cairo-gl` over Mesa/EGL/gbm when available.
3. `O2Context_skia` â€” **opt-in, post-M6**, once the semantics are pinned by a passing pixel-diff test suite. The test suite is what makes the swap safe; write it against Cairo, cash it in against Skia.

The vtable is not speculative architecture â€” **Cocotron already has it.** `O2Context` is abstract with concrete subclasses (`O2Context_builtin`, `O2Context_gdi`, â€¦). You are filling in a slot the design already anticipated.

### 3.3 The three places the mapping does *not* close cleanly

Budget for these explicitly; they are where naive ports die.

- **`CGShading` with arbitrary `CGFunction`.** CG lets an app supply a C callback that evaluates color at parameter `t`. Cairo gradients are stop-based only. **Mitigation (this is what Opal does): sample the callback into N stops** â€” adaptive, ~64â€“256 depending on measured curvature. Exact for the common linear/ramp cases; imperceptible otherwise. Skia has the same limitation.
- **`CGColorSpace` breadth.** Device/Generic/sRGB are easy. ICC-based, Indexed, and Pattern spaces are not. **Mitigation: `lcms2` (MIT)** â€” the same choice Opal made. Note Opal's own `TODO` still lists colorspace management as incomplete after 15 years; do not underestimate this, but *do* stub it aggressively (assume sRGB) for M1â€“M4.
- **Font smoothing / subpixel positioning.** The six `CGContextSet{Should,Allows}Font*` calls are stateful no-ops you can accept-and-ignore for a long time, but `CGContextSetShouldSubpixelPositionFonts` genuinely changes glyph raster positions and therefore text metrics. **Mitigation: implement subpixel positioning in FreeType from the start** (it supports it); retrofitting causes a full text-layout regression.

### 3.4 Font and text seam

`CGFont` â†’ FreeType. **CoreText sits *above* CoreGraphics, not beside it** â€” `CTFontRef` wraps `CGFontRef`, and `CTLine`/`CTRun` ultimately call `CGContextShowGlyphsAtPositions`. Both Cocotron (`O2Font_freetype`, plus a `CoreText` framework dir) and Opal (`OpalText`) have partial CoreText.

Be honest about scale: Opal's `TODO` names OpalText as "the most extensive work remaining â€” requires completing font descriptor classes, implementing the `CTTypesetter` core (including Unicode linebreaking, text itemization, and HarfBuzz integration for OpenType layout), and finishing `CTFramesetter`." **Use HarfBuzz (MIT) for shaping and ICU or `libunibreak` for linebreaking. Do not write these.** You already ship HarfBuzz via Pango.

---

## 4. Binary compatibility â€” the exact requirements

Source compatibility is a compiler problem. Binary compatibility is a **linker, loader, and ABI** problem. These are the things that must be exactly right or a real `.app` will not load.

### 4.1 install_name and version fields

The framework's `LC_ID_DYLIB` must read exactly:

```
/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
```

Under **two-level namespace** (the macOS default), every undefined symbol in the app records a *library ordinal* pointing at a specific `LC_LOAD_DYLIB` entry. dyld resolves `_CGContextFillRect` **only** from the library whose install_name matches what the app recorded. A correct implementation at the wrong path is invisible to dyld.

Also required:
- **`compatibility_version` must be â‰¥ what the app recorded.** dyld hard-fails otherwise. Set it high and never lower it. Read the real value from the SDK `.tbd` (`compatibility-version:` field) rather than guessing.
- **`current_version`** â€” cosmetic to dyld, but some apps `NSVersionOfLinkTimeLibrary()` it.
- **`LC_BUILD_VERSION` platform = macOS**, minos â‰¤ target.
- **Framework bundle layout on disk**: `Versions/A/CoreGraphics`, `Versions/A/Resources/Info.plist`, `Versions/Current â†’ A`, `CoreGraphics â†’ Versions/Current/CoreGraphics`. `CFBundleIdentifier` = `com.apple.CoreGraphics` (some apps `CFBundleGetBundleWithIdentifier` it).

### 4.2 The re-export graph

Many apps do not link CoreGraphics directly â€” they link `ApplicationServices.framework`, which **re-exports** CoreGraphics via `LC_REEXPORT_DYLIB`. Note the historical shape visible in the SDKs: in 10.6 CoreGraphics lived *inside* ApplicationServices ([`ApplicationServices.framework/Versions/A/Frameworks/CoreGraphics.framework/â€¦`](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/CoreGraphics.framework/Versions/A/Headers/CGPath.h)); by 10.8+ it is top-level. **You must replicate the re-export edges, not just the leaf frameworks**, or symbol resolution silently fails for older binaries. Extract the graph from the SDK `.tbd` files' `reexported-libraries` sections.

### 4.3 Struct layout and calling convention â€” x86_64 System V

Verified from [`CGBase.h`](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.9.sdk/System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers/CGBase.h): under `__LP64__`, `CGFLOAT_TYPE` is `double`, `CGFLOAT_IS_DOUBLE` is `1`, so **`CGFloat` is `double` (8 bytes) on your target**. And from [`CGGeometry.h`](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.9.sdk/System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers/CGGeometry.h): `CGPoint{CGFloat x,y}`, `CGSize{CGFloat width,height}`, `CGVector{CGFloat dx,dy}`, `CGRect{CGPoint origin; CGSize size}`.

That yields, under the SysV AMD64 ABI:

| Type | Size | SysV class | Passed | Returned |
|---|---|---|---|---|
| `CGFloat` | 8 | SSE | `xmm` reg | `xmm0` |
| `CGPoint` / `CGSize` / `CGVector` | 16 | SSE, SSE | two `xmm` regs | `xmm0`:`xmm1` |
| `CGRect` | 32 | **MEMORY** (>16 bytes) | on the **stack** | via hidden **sret** pointer |
| `CGAffineTransform` | 48 | **MEMORY** | stack | sret |

**The Objective-C consequence, which bites everyone exactly once:** on x86_64, an ObjC method returning `CGRect`/`NSRect` is dispatched through **`objc_msgSend_stret`**, while one returning `CGPoint`/`CGSize` uses plain **`objc_msgSend`**. If your AppKit layer gets this wrong you get garbage rects and no crash. Since `NSRect`/`NSPoint`/`NSSize` are `typedef`s of the CG types on 64-bit, layout is automatically identical â€” but **the dispatch path is not automatic.** Write an ABI conformance test that calls a `CGRect`-returning method through `objc_msgSend_stret` and asserts field values.

Add a static-assert header to CI:
```c
_Static_assert(sizeof(CGFloat) == 8, "");
_Static_assert(sizeof(CGPoint) == 16 && _Alignof(CGPoint) == 8, "");
_Static_assert(sizeof(CGRect) == 32 && offsetof(CGRect, size) == 16, "");
_Static_assert(sizeof(CGAffineTransform) == 48, "");
```

### 4.4 CFType registration â€” do not skip this

On real macOS, `CGContextRef`, `CGImageRef`, `CGColorRef`, `CGColorSpaceRef`, `CGPathRef` etc. are **CFRuntime-registered CFTypes**, not ObjC objects. Apps rely on this: they call `CFRetain`/`CFRelease` on them, stuff them in `CFArray`/`NSArray` (which retains via `CFRetain`), compare `CFGetTypeID(x) == CGImageGetTypeID()`, and pass them to `CFShow`.

**This is where you diverge from Cocotron and gain.** Cocotron's `O2Context` is a plain ObjC object because Cocotron had no real CoreFoundation. **OpenOSX has CoreFoundation 1338.** Register real `CFRuntimeClass` entries so `CGContextGetTypeID()` returns a genuine `CFTypeID`. Concretely: keep Onyx2D's ObjC `O2*` objects internally, but make the CG-level handles real CFTypes that own an `O2*` â€” or, cleaner, register the `O2*` classes with the CF runtime bridge. Either way, **`CFRetain(cgimage)` must work**, and that is a hard requirement no amount of source compatibility gives you.

### 4.5 The `CGS*` private surface â€” handle with care

`CGSConnectionID`, `CGSWindowID`, `CGSNewWindow`, `CGSSetWindowTransform`, `CGSFlushWindow` etc. are **undocumented**. There is no public header. Community reverse-engineered headers exist ([NUIKit/CGSInternal](https://github.com/NUIKit/CGSInternal), [CocoaDev CoreGraphicsPrivate](https://cocoadev.github.io/CoreGraphicsPrivate/)) and are legitimate published RE write-ups â€” legal input under your constraint.

**But: you are writing the AppKit above it too.** So do not chase CGS fidelity. Define **your own** `CGS`-shaped internal protocol between your AppKit and your WindowServer, matching CGS only where a *third-party* binary is observed to call it. Only real apps that poke CGS directly (window managers, screen recorders, some Electron builds) need bug-compatibility, and those are a later, separately-scoped problem. Darling's plan â€” "AppKit does CGS calls; CoreGraphics backends implement the CGS interfaces" â€” is the right shape; just do not treat Apple's exact CGS as a spec you owe.

### 4.6 How to measure required symbol coverage â€” concretely

**Source A: the SDK `.tbd` (authoritative target).** Per [Apple's TAPI docs](https://github.com/apple-opensource/tapi/blob/master/docs/TBD.rst), a `.tbd` is "a textual, human readable representation of Mach-O dynamic librariesâ€¦ holding properties needed to resolve static link time dependencies including the same exported symbols as the original dynamic library." v1â€“v3 are YAML, v5+ is JSON. It carries `install-name`, `current-version`, `compatibility-version`, `reexported-libraries`, and `exports:` with `symbols`, `objc-classes`, `objc-ivars`, `weak-symbols`. **This is your spec file.** Parse it directly. Note [PureDarwin already maintains a `tapi` fork](https://github.com/PureDarwin/tapi) â€” you likely have the tooling in-tree already.

**Source B: real app binaries (prioritization).** For a corpus of `.app` bundles:
```bash
# 1. Which libraries does it need at all?
otool -L Foo.app/Contents/MacOS/Foo

# 2. Undefined symbols, annotated with the two-level namespace library:
nm -mu -arch x86_64 Foo.app/Contents/MacOS/Foo | grep CoreGraphics

# 3. Stripped-binary-safe (dyld info survives strip):
dyld_info -bind -weak_bind -lazy_bind Foo.app/Contents/MacOS/Foo
llvm-objdump --macho --bind --lazy-bind Foo.app/Contents/MacOS/Foo
```
`nm` reads the `nlist` symbol table, which `strip -T` can gut; `LC_DYLD_INFO_ONLY` bind opcodes survive stripping. **Prefer `dyld_info`; keep `nm -mu` for the readable library attribution.**

**Source C: your own build (provided set).**
```bash
nm -gU CoreGraphics.framework/Versions/A/CoreGraphics   # defined externals
dyld_info -exports CoreGraphics.framework/Versions/A/CoreGraphics
```

**The harness (build this in P0):**
```
required   := parse(CoreGraphics.tbd).exports.symbols
demanded   := â‹ƒ over corpus of (undefined syms attributed to CoreGraphics)
provided   := export trie of our built dylib

missing_hard := demanded \ provided     # blocks a real app RIGHT NOW â€” rank by app count
missing_spec := required \ provided     # long-tail completeness metric
extra        := provided \ required      # ABI pollution â€” should be empty
```
Emit `missing_hard` ranked by how many corpus apps demand it. **That ranking is your work queue.** It replaces every architectural argument about "what to implement next" with a number.

**Two traps, learned from Darling.** Their [stub-generation docs](https://docs.darlinghq.org/contributing/generating-stubs.html) note the generator "does not currently generate symbols for constants. Those must be manually added." **Data symbols are exports too** â€” `kCGColorSpaceSRGB`, `CGPointZero`, `CGRectNull`, `kCGImagePropertyDPIWidth`. A missing data symbol is a dyld hard-fail identical to a missing function, and symbol-counting tools routinely miss them because they only look at text. Second trap: Darling's generator can't read the dyld shared cache â€” irrelevant to you (you're building the dylib, not extracting it), but it explains why their coverage data is patchy.

### 4.7 The abort-stub trick (do this in week one)

Auto-generate, from the `.tbd`, a `CoreGraphics.dylib` with the correct install_name and **every** export present as:

```c
void CGContextDrawShading(void) {
    fprintf(stderr, "[CG-STUB] CGContextDrawShading\n");
    abort();
}
```

This costs a day and buys enormous leverage: real `.app` binaries now **get past dyld** and run until they touch CG, and stderr gives you the *exact, ordered, observed* call trace of what a real app actually needs. That is empirical prioritization instead of speculation, on day one, before a single pixel is rasterized. Then replace stubs with implementations, ranked by trace frequency, with a CI gate on `stub_calls_remaining`.

---

## 5. Phased plan â€” minimum CG to draw an AppKit window

### The target milestone

An `NSWindow` containing an `NSView` whose `-drawRect:` fills a rect, strokes a bezier path, and draws a string â€” appearing on screen, composited by your existing wlroots/xfwm4 stack.

### The critical path (what actually has to work)

```
NSView -drawRect:
  â†’ NSGraphicsContext.currentContext.CGContext        (a CGBitmapContext)
  â†’ CGContext* calls
  â†’ O2Context_cairo â†’ cairo_image_surface (ARGB32, premultiplied)
  â†’ shared buffer (PDSurface / wl_shm)                â† zero-copy, Â§1.5
  â†’ WindowServer â†’ wlroots/xfwm4 â†’ Mesa â†’ screen
```

`NSGraphicsContext` is documented as "a wrapper for a Quartz graphics context (`CGContextRef`)," where "many methodsâ€¦ simply call their Quartz equivalents" ([Cocoa Drawing Guide](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CocoaDrawingGuide/GraphicsContexts/GraphicsContexts.html)). So **one `CGBitmapContext` per window backing store** is the whole design. Everything else is detail.

### Phases

| Phase | Scope | Symbols (est.) | Exit criterion |
|---|---|---|---|
| **P0 â€” Instrumentation** (2â€“3 wk) | `.tbd` parser; corpus scanner; coverage differ; **abort-stub dylib** with correct install_name/compat-version; framework bundle layout; ABI static-asserts; CI gates | ~all (as stubs) | A real `.app` loads under dyld and prints an ordered `[CG-STUB]` trace before aborting. **This alone is a milestone worth announcing.** |
| **P1 â€” Pure math** (2 wk) | `CGGeometry` (~45 fns), `CGAffineTransform`, `CGError`, `CGBase`. No backend, no state. | ~70 | Unit tests vs documented semantics. `CGRectIntegral`, `CGRectDivide`, `CGRectNull` edge cases green. |
| **P2 â€” Object model** (3â€“4 wk) | CFRuntime registration for all CG types (Â§4.4); `CGColorSpace` (Device/Generic/sRGB only), `CGColor`, `CGDataProvider`/`Consumer`, `CGPath`/`CGMutablePath` + `CGPathApply` | ~180 | `CFRetain`/`CFRelease`/`CFGetTypeID` work on every CG handle. No leaks under a stress harness. |
| **P3 â€” Rasterization** (6â€“8 wk) â˜… | `CGBitmapContext` + `CGContext` core: gstate stack, CTM, path construction + drawing, clip (incl. `ClipToMask`), solid colors, alpha, all `kCGBlendMode*`, transparency layers, `CGLayer`. Wire `O2Context_cairo`. | ~250 | **Render to PNG from CG calls; pixel-diff vs golden images.** Build the golden suite here â€” it is what later makes the Skia swap safe. |
| **P4 â€” Images** (3â€“4 wk) | `CGImage` full, `CGImageSource`/`CGImageDestination` seam (ImageIO), `CGContextDrawImage`/`DrawTiledImage`, interpolation quality, `CGGradient`, `CGShading` (function sampling, Â§3.3), `CGPattern` | ~180 | Draw a PNG into a context, tiled, transformed, clipped, blended â€” pixel-exact. |
| **P5 â€” Text** (6â€“8 wk) | `CGFont` via FreeType, `CGGlyph`, advances, glyph paths, text matrix, `ShowGlyphsAtPositions`, text drawing modes, subpixel positioning (Â§3.3). CoreText seam via HarfBuzz. | ~120 CG + CoreText | A string renders at correct metrics and position. |
| **P6 â€” Window surface** (4â€“6 wk) | Internal `CGS`-shaped protocol; window backing store as `CGBitmapContext` over shared memory; flush/damage; `CGDirectDisplay` basics; WindowServer wiring | ~80 + private | **â˜… The AppKit hello-world window appears on screen.** |
| **P7 â€” Acceleration & polish** | `O2Context_skia` behind the vtable, GPU paths via Mesa/virgl, real ICC via lcms2, `CGPDF*`, shadows, CoreAnimation seam | â€” | MotionMark-style benchmark; golden suite still green on the new backend. |

**Estimated minimum viable surface: ~700â€“800 exported symbols** to get a window drawing. Full public CoreGraphics is several thousand, and with the private `CGS`/display/event surface, substantially more. **Treat these as hypotheses to falsify with the P0 harness in week three, not as plan inputs.**

### Sequencing notes

- **P0 before everything.** It is the cheapest phase and it converts the rest of the roadmap from guesswork into a ranked list. Skipping it is the single most likely way this project stalls â€” it is precisely what Darling never built, and why their coverage has drifted for a decade.
- **P3 is the long pole and the real risk.** Everything before it is mechanical; everything after depends on it. Budget generously and build the golden-image suite *during* P3, not after.
- **P5 (text) is where projects go to die.** Opal's `TODO` still lists OpalText as its largest gap after 15 years of development. Do not write shaping or linebreaking â€” HarfBuzz and libunibreak, both already in your image via Pango.
- **P6 unlocks the demo.** Do not let perfect CG hold it hostage; a window with a filled rect and a stroked path on screen is worth more to the project's momentum than complete `CGPDF` support.

### Immediate next actions

1. Vendor `Frameworks/CoreGraphics` and `Frameworks/Onyx2D` from ravynOS `main` (MIT headers, BSD-2 tree) into `src/Libraries/`, preserving all copyright notices, and record the license election for Cairo (MPL-1.1) in a `LICENSES/` manifest.
2. Write `tools/cg-coverage/` â€” the `.tbd` parser + `dyld_info` corpus scanner + differ.
3. Generate the abort-stub `CoreGraphics.dylib`; get a real `.app` to load and produce a call trace.
4. Add ABI static-asserts and the `objc_msgSend_stret` conformance test to CI.
5. Add the clean-room contributor attestation to `CONTRIBUTING.md` for the graphics subtree.

---

## Sources

- [Quartz 2D Programming Guide â€” Overview](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/drawingwithquartz2d/dq_overview/dq_overview.html)
- [Quartz 2D Programming Guide â€” Graphics Contexts](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/drawingwithquartz2d/dq_context/dq_context.html)
- [Cocoa Drawing Guide â€” Graphics Contexts](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CocoaDrawingGuide/GraphicsContexts/GraphicsContexts.html)
- [Core Text Overview](https://developer.apple.com/library/archive/documentation/StringsTextFonts/Conceptual/CoreText_Programming/Overview/Overview.html)
- [SDK header: CGContext.h](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.9.sdk/System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers/CGContext.h) Â· [CGGeometry.h](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.9.sdk/System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers/CGGeometry.h) Â· [CGBase.h](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.9.sdk/System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers/CGBase.h) Â· [CoreGraphics.h umbrella](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.8.sdk/System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers/CoreGraphics.h) Â· [CGPath.h (10.6, inside ApplicationServices)](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/CoreGraphics.framework/Versions/A/Headers/CGPath.h)
- [ravynOS](https://github.com/ravynsoft/ravynos) Â· [Technical Details](https://ravynos.com/more/) Â· [Frameworks/CoreGraphics](https://github.com/ravynsoft/ravynos/tree/main/Frameworks/CoreGraphics) Â· [CGContext.m (MIT header)](https://github.com/ravynsoft/ravynos/blob/main/Frameworks/CoreGraphics/CGContext.m) Â· [CoreServices/WindowServer](https://github.com/ravynsoft/ravynos/tree/main/CoreServices/WindowServer) Â· [0.5.0 release notes](https://github.com/ravynsoft/ravynos/releases/tag/v0.5.0)
- [Cocotron](https://github.com/cjwl/cocotron) Â· [Onyx2D](https://github.com/cjwl/cocotron/tree/master/Onyx2D) Â· [O2Context.m](https://github.com/cjwl/cocotron/blob/master/Onyx2D/O2Context.m) Â· [cocotron.org](https://cocotron.org/) Â· [The MIT License](https://cocotron.com/Info/The_MIT_License)
- [GNUstep Opal](https://github.com/gnustep/libs-opal) Â· [Opal TODO](https://github.com/gnustep/libs-opal/blob/master/TODO) Â· [Opal CGContext.h](https://github.com/gnustep/libs-opal/blob/master/Headers/CoreGraphics/CGContext.h)
- [darling-coregraphics](https://github.com/darlinghq/darling-coregraphics) Â· [darling-appkit](https://github.com/darlinghq/darling-appkit) Â· [Issue #937 â€” Cocotron (AppKit) backend rework](https://github.com/darlinghq/darling/issues/937) Â· [Generating stubs](https://docs.darlinghq.org/contributing/generating-stubs.html) Â· [Darling: macOS compatibility for Linux (LWN)](https://lwn.net/Articles/794871/)
- [Skia](https://github.com/google/skia) Â· [SkCanvas Creation](https://skia.org/docs/user/api/skcanvas_creation/) Â· [Release notes](https://github.com/google/skia/blob/main/RELEASE_NOTES.md)
- [Replacing Cairo in WebKit with Skia (webkit-dev)](https://lists.webkit.org/pipermail/webkit-dev/2024-February/032615.html) Â· [Igalia: WebKit switching to Skia](https://blogs.igalia.com/carlosgc/2024/02/19/webkit-switching-to-skia-for-2d-graphics-rendering/) Â· [WebKitGTK 2.46](https://webkitgtk.org/2024/10/04/webkitgtk-2.46.html) Â· [Graphics improvements after the switch](https://blogs.igalia.com/carlosgc/2025/04/21/graphics-improvements-in-webkitgtk-and-wpewebkit-after-the-switch-to-skia/)
- [Cairo Graphics](https://www.cairographics.org/) Â· [Cairo (Wikipedia) â€” LGPL-2.1 / MPL-1.1](https://en.wikipedia.org/wiki/Cairo_(graphics)) Â· [Cairo COPYING](https://github.com/vishnubob/cairo/blob/master/COPYING)
- [TAPI TBD format docs](https://github.com/apple-opensource/tapi/blob/master/docs/TBD.rst) Â· [tapi-tbdv5(1)](https://keith.github.io/xcode-man-pages/tapi-tbdv5.1.html) Â· [PureDarwin/tapi](https://github.com/PureDarwin/tapi) Â· [tbdump](https://github.com/Siguza/tbdump) Â· [ld(1)](https://keith.github.io/xcode-man-pages/ld.1.html)
- [mikeash: dyld â€” Dynamic Linking On OS X](https://www.mikeash.com/pyblog/friday-qa-2012-11-09-dyld-dynamic-linking-on-os-x.html) Â· [Mach-O format (HackTricks)](https://hacktricks.wiki/en/macos-hardening/macos-security-and-privilege-escalation/macos-files-folders-and-binaries/universal-binaries-and-mach-o-format.html) Â· [Mach-O binaries / LC_DYLD_INFO](http://www.m4b.io/reverse/engineering/mach/binaries/2015/03/29/mach-binaries.html) Â· [Exporting Your Framework Interface](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPFrameworks/Tasks/ExportingInterfaces.html)
- [NUIKit/CGSInternal â€” CGSConnection.h](https://github.com/NUIKit/CGSInternal/blob/master/CGSConnection.h) Â· [CGSWindow.h](https://github.com/NUIKit/CGSInternal/blob/master/CGSWindow.h) Â· [CocoaDev: CoreGraphicsPrivate](https://cocoadev.github.io/CoreGraphicsPrivate/)
- [CGBitmapContext Reference](https://leopard-adc.pepas.com/documentation/GraphicsImaging/Reference/CGBitmapContext/Reference/reference.html)
- [PureDarwin Graphics wiki](https://github.com/PureDarwin/PureDarwin/wiki/Graphics)

