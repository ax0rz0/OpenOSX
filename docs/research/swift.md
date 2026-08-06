# Swift Runtime Gap Analysis for OpenOSX

**Scope:** feasibility of shipping a Swift runtime in OpenOSX (Darwin 20.5 / x86_64), built from open source, capable of loading Apple-built Swift binaries.

**Bottom line up front:**

1. **`libswiftCore` and the rest of the *core* runtime are buildable from open source and are legally shippable** (Apache 2.0 + Runtime Library Exception). This is a real, bounded, ~1â€“2 month project and it is **independent of the AppKit work** â€” do it in parallel, not after.
2. **Every Darwin *overlay* â€” `libswiftDarwin`, `libswiftObjectiveC`, `libswiftDispatch`, `libswiftFoundation`, `libswiftAppKit`, `libswiftCoreGraphics` â€” was deliberately removed from the open-source repo** and now ships only in Apple's SDK/OS. I confirmed this directly against the repo tree (see Â§2.2). These must be reimplemented.
3. The Swift gap is **not the bottleneck**. `libswiftFoundation` is gated on having a real Objective-C Foundation (OpenOSX currently ships ~6 source files' worth), and `libswiftAppKit` is gated on AppKit existing. Swift adds roughly **15â€“20% on top of** the Foundation+AppKit effort, not a separate mountain.
4. There is one very high-leverage technique that I want to flag up front and that I have not seen discussed in the Darwin-reimplementation community: **the macOS SDK ships textual `.swiftinterface` files for every overlay.** These are *declaration-level* artifacts â€” the same category as the public SDK headers OpenOSX already builds against â€” and `swiftc` can compile them directly into a dylib with a **correct-by-construction exported symbol set and type metadata**. See Â§5.2. This converts "reverse-engineer the Swift ABI of AppKit" into "write the bodies." It also carries a legal question that needs answering before anything derived from it ships.

---

## 1. What a Swift binary actually needs on macOS 11

### 1.1 The four tiers

Swift's runtime dependencies are not one library. They split into four tiers with very different implications for OpenOSX:

| Tier | Examples | Ships where | Open source? | OpenOSX action |
|---|---|---|---|---|
| **A â€” Core runtime** | `libswiftCore.dylib`, `libswiftSwiftOnoneSupport.dylib`, `libswiftRemoteMirror.dylib`, `libswift_Concurrency.dylib` | `/usr/lib/swift/` in the OS | **Yes** (Apache 2.0 + RLE) | **Build it.** Tractable now. |
| **B â€” Platform overlays** | `libswiftDarwin`, `libswiftObjectiveC`, `libswiftDispatch`, `libswiftCoreFoundation`, `libswiftos`, `libswiftsimd`, `libswiftXPC`, `libswiftIOKit` | `/usr/lib/swift/` in the OS | **No** â€” removed from repo | Reimplement (thin; Â§5.2) |
| **C â€” Framework overlays** | `libswiftFoundation`, `libswiftAppKit`, `libswiftCoreGraphics`, `libswiftQuartzCore`, `libswiftMetal`, `libswiftAVFoundation`, `libswiftCoreImage`, `libswiftWebKit`, `libswiftVision`, â€¦ | `/usr/lib/swift/` in the OS | **No** | Gated on the underlying ObjC framework existing |
| **D â€” Compatibility shims** | `libswiftCompatibility50.a`, `â€¦51.a`, `â€¦DynamicReplacements.a`, `â€¦Concurrency.a` | **Statically linked into the app binary** | Yes (in-repo) | **Nothing to do** â€” already inside every app you'd run |

Tier D is a free win worth knowing about: these are static archives from the developer's toolchain that get baked into the app at link time. They carry runtime bug fixes for older OS runtimes. Their presence in the app means an Apple-built binary already carries some of its own patch layer ([swift-evolution discussion](https://forums.swift.org/t/could-not-find-or-use-auto-linked-library-swiftcompatibility50/54351), [swift PR #25473](https://github.com/swiftlang/swift/pull/25473/files)).

### 1.2 What the dependency set looks like in practice

- **A bare Swift CLI tool** (`print("hi")`, `import Swift` only) â†’ `libswiftCore.dylib`, plus `libSystem.B.dylib`, `libobjc.A.dylib`, `libc++.1.dylib`. **OpenOSX has all three of the non-Swift ones today.** This is the target for Phase S0.
- **Any tool that does `import Foundation`** â†’ pulls in `libswiftFoundation` â†’ `libswiftCoreFoundation`, `libswiftObjectiveC`, `libswiftDispatch`, `libswiftDarwin`, `libswiftXPC`, `libswiftIOKit`, `libswiftos`. This is the realistic floor for "useful CLI tools," and it is a much bigger job.
- **Debug builds** additionally want `libswiftSwiftOnoneSupport.dylib` (prespecialized generics for `-Onone`); release builds generally do not.

### 1.3 Where the runtime lives, and the Big Sur wrinkle

Since Swift 5.0 / macOS 10.14.4, the runtime is an OS component with install names under `/usr/lib/swift/`, e.g. `/usr/lib/swift/libswiftCore.dylib` (compatibility version `1.0.0`). Apple's framing: the runtime is *"a component of the user's target operating system rather than part of the developer's toolchain"* ([Swift.org](https://www.swift.org/blog/abi-stability-and-apple/)).

**Important for testing:** on Big Sur and later these files **do not exist on disk** â€” they live only in the dyld shared cache, and directory enumeration finds nothing. Presence must be probed with `dlopen()` ([Apple DTS](https://developer.apple.com/forums/thread/655588)). Two consequences for OpenOSX:

- If you dump a real 11.x Mac to enumerate the overlay set, you need `dyld_shared_cache_util`, not `ls`.
- OpenOSX will ship **loose dylibs** with no shared cache. That is functionally fine â€” dyld falls back to on-disk lookup â€” but you lose the cache's prebuilt closures and page-sharing, so Swift process launch will be measurably slower. Not a blocker; worth knowing before someone files it as a bug.

### 1.4 Concurrency: a version trap specific to Darwin 20

`libswift_Concurrency.dylib` **is not in macOS 11.** It first shipped in the OS with macOS 12. Apps targeting macOS 11 that use `async/await` copy a back-deployed concurrency dylib into their own `Frameworks/` directory (Xcode 13.2+, back-deploys to macOS 10.15) ([Swift Forums](https://forums.swift.org/t/swift-5-5-2-xcode-13-2-beta-fails-to-link-libswift-concurrency-dylib/53263), [nonstrict.eu](https://nonstrict.eu/blog/2023/using-async-await-in-a-commandline-tool-on-older-macos-versions/)).

This cuts both ways for OpenOSX:

- **Good:** for a macOS-11-shaped target, apps bring their own concurrency runtime. You may not need to ship one at all initially.
- **Bad:** the moment you want to run apps built for macOS 12+, you owe a system `libswift_Concurrency` â€” and the Darwin build of Swift concurrency drives its cooperative thread pool through **libdispatch SPI**, not plain `dispatch_async`. Your libdispatch will need those entry points. The in-repo `BackDeployConcurrency` variant (confirmed present, Â§2.2) uses the plain-dispatch path and is the correct thing to build first.

---

## 2. Can the runtime be built from open source, for Darwin x86_64?

### 2.1 Yes â€” and there is existence proof

Swift.org publishes **open-source macOS toolchains** whose `usr/lib/swift/macosx/` contains a `libswiftCore.dylib` built entirely from the Apache-2.0 `swiftlang/swift` repository. Toolchains install to `/Library/Developer/Toolchains` and binaries can be made to load the toolchain copy via rpath instead of the OS copy ([swift.org install docs](https://www.swift.org/install/macos/package_installer/), [Ole Begemann](https://oleb.net/2024/swift-toolchains/)). So "an open-source-built libswiftCore for Darwin x86_64" is not speculative â€” it is a shipped artifact today. The question is only whether OpenOSX can produce and host one.

### 2.2 What is actually in the repo â€” verified

I queried the GitHub contents API for `stdlib/public` on `release/5.5` (the branch matching the macOS 11 / Swift 5.4â€“5.5 era). Directories present:

```
BackDeployConcurrency   CompatibilityOverride   Concurrency   Differentiation
LLVMSupport             Platform                Reflection    SwiftOnoneSupport
SwiftRemoteMirror       SwiftShims              Windows       core
runtime                 stubs
```

**`Darwin` is absent â€” `stdlib/public/Darwin` returns HTTP 404 on that branch.** This is the single most important fact in this report. Apple removed the SDK overlays from the open-source build deliberately; the rationale given upstream was that the overlays *"needed to exactly match with the SDK being used, despite not actually being necessary now that Swift overlays ship as part of the frameworks to which they belong,"* and that *"the SDK version has diverged from the Swift version making them incompatible."*

So the buildable set is exactly: **`libswiftCore`, `libswiftSwiftOnoneSupport`, `libswift_Concurrency` (both variants), `libswiftRemoteMirror`, `libswiftDifferentiation`.** Everything with a Darwin framework name is not in the box.

### 2.3 Blocker 1 â€” the build host (this is the real engineering risk)

The upstream Getting Started guide states plainly for macOS: *"Install Xcode 12.3 or newer,"* plus CMake/Ninja/Sccache; 3.5 GB of source and **5â€“70 GB of build artifacts**; build times "a few minutes to several hours." The guide **does not mention cross-compilation or building for a non-host target at all.**

And the direction OpenOSX needs is the unsupported one. The official Swift SDK generator supports *macOS host â†’ Linux target*; **Linux host â†’ macOS target is not supported** ([swift-sdk-generator](https://github.com/apple/swift-sdk-generator/), [SE-0387](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0387-cross-compilation-destinations.md)).

OpenOSX's entire bootchain is the opposite: a Nix cross toolchain on Linux targeting `x86_64-apple-darwin20.4`, driven from `nix/pkgs/apple/*.nix`, consuming a locally-registered `MacOSX11.3.sdk` (see `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\apple\foundation.nix`).

Two viable paths:

- **Path A â€” use the Mac as a build node.** Task #14 ("Wire Mac as SSH build/test node") already exists and this is exactly the workload it is right for. `swiftc` on a Mac can target `x86_64-apple-macos11.0` with an explicit `-sdk`, and the resulting dylib is a normal Mach-O you can drop into the OpenOSX image. **This is the low-risk path and I recommend it for Phase S0.** It also gives you the ideal test rig: build an Apple-toolchain Swift binary on the Mac, copy it into the OpenOSX VM, and see whether it loads against your runtime. That is the actual acceptance test for this entire workstream.
- **Path B â€” teach the cross toolchain to build the stdlib.** Architecturally this is sound: `swiftc` is inherently a cross compiler (front end takes `-target` and `-sdk`), and the stdlib build is ultimately clang + swiftc + ld64, all of which the Nix cross toolchain already has. The friction is entirely in `build-script`/CMake, which branches on `host == Darwin` for the Darwin stdlib and reaches for `xcrun`. Expect to fight the build system, not the compiler. Worth doing eventually so CI is self-hosting, but do not make Phase S0 depend on it.

### 2.4 Blocker 2 â€” Objective-C interop against OpenOSX's objc4

A Darwin-configured `libswiftCore` is compiled `-enable-objc-interop` and hard-depends on the ObjC runtime. Specifically it needs objc4's **Swift-support hooks**: the Swift metadata initializer callback, `_objc_realizeClassFromSwift`, `objc_setHook_getClass` / `getImageName`, non-pointer isa, tagged pointers, and the `_dyld_register_func_for_add_image` / `objc_addLoadImageFunc` image-notification path.

**Good news: this is the part OpenOSX is best positioned for.** These hooks are all present in the open-source objc4 of the Big Sur era, and `nix/pkgs/apple/libobjc.nix` already builds it. This blocker is more "verify carefully" than "implement." Concretely, Phase S0 should include a symbol-level audit of what a built `libswiftCore` imports from `libobjc.A.dylib` and `libSystem.B.dylib` versus what OpenOSX's versions export â€” that audit is cheap and will surface any surprises before you sink weeks in.

Watch specifically for: `os_unfair_lock_*`, `malloc_size`, `mach_absolute_time`, `dispatch_once_f`, pthread TSD slots, and `_dyld_*` image callbacks.

### 2.5 Blocker 3 â€” ICU (smaller than it looks)

The stdlib historically linked `libicucore` on Darwin for normalization (UAX15) and grapheme breaking (UAX29). Swift then moved to its own embedded Unicode tables, with the ICU dependency fully removed by around **Swift 5.8** ([Swift 5.8 release notes](https://www.swift.org/blog/swift-5.8-released/)); on Apple platforms the stdlib's grapheme breaking tracks the Unicode version of the corresponding OS release.

For OpenOSX: `nix/pkgs/apple/icucore.nix` already exists, so the 5.4/5.5-era dependency is satisfiable. But this is a **concrete argument for building a newer stdlib than 5.4** â€” a 5.8+ stdlib drops an entire external dependency and removes a whole class of "which ICU version, and does its symbol renaming match" problems. See Â§3.2, which pushes the same direction for a different reason.

### 2.6 Blocker 4 â€” Foundation, and why swift-corelibs-foundation is a trap

This is where the wall is.

`swift-corelibs-foundation` (Apache 2.0) looks like the obvious answer and **is not**. Three independent reasons:

1. **It explicitly builds for non-Darwin platforms only.** Its own guidance is that on Apple platforms you use the OS Foundation.
2. **Apple has stopped supporting standalone CoreFoundation builds.** This was confirmed directly to the PureDarwin project. Cliff Sekel of PureDarwin asked on the Swift forums whether CF could still be built standalone for a non-Apple Darwin system; Tony Parker of the Swift team replied that they have focused the CF sources on *supporting the Swift sources as an implementation detail, rather than focusing on building them standalone* ([Swift Forums](https://forums.swift.org/t/building-corefoundation-for-darwin-based-oss/75378)). Apple is also progressively reimplementing C-level CF classes (CFCalendar, CFTimeZone, CFLocale) to delegate *upward* into Swift. **This is a direct, on-the-record answer to OpenOSX's exact question, from the people who maintain the code, and it should be treated as load-bearing: this door is closed and will keep closing.**
3. **Class-name collision.** swift-corelibs-foundation defines `NSString`, `NSArray`, etc. as Swift-native classes. OpenOSX has a real ObjC `NSString`/`NSCFString` toll-free bridged to CFString (`nix/pkgs/apple/foundation.nix` builds exactly `NSString`, `NSCFString`, `NSArray`, `NSDictionary`, `NSURL`, `NSError`). Loading both into one process is a runtime class-collision disaster, and the bridging semantics differ.

The correct architecture is the one Apple itself converged on: a Swift-level Foundation overlay **on top of** a real ObjC Foundation. Which means the prerequisite is *a real ObjC Foundation*, and OpenOSX's current Foundation is a deliberate six-file slice. **`libswiftFoundation` is therefore not a Swift project. It is a Foundation project wearing a Swift hat**, and it should be scheduled as such.

One genuinely promising long-term angle: **`swift-foundation` (`FoundationEssentials`, Apache 2.0, pure Swift)** is Apple's rewrite and is explicitly designed to be the shared implementation across platforms, split into `FoundationEssentials` / `FoundationInternationalization` / `FoundationNetworking` / `FoundationXML` / `FoundationObjCCompatibility` ([InfoQ](https://www.infoq.com/news/2022/12/apple-swift-foundation-rewrite/)). If OpenOSX ever needs a Foundation *implementation* rather than a Foundation *ABI*, this is Apache-2.0 and is the modern, maintained, non-dead-end option. It does not solve binary compatibility with Apple-built binaries, but it could underpin OpenOSX-native Swift software.

---

## 3. ABI fidelity: will Apple-built Swift binaries load against an open-built runtime?

### 3.1 In principle, yes â€” that is what ABI stability *means*

Swift's stable ABI is a specification comprising: **symbol mangling, calling conventions, type layout, value witness tables, and runtime metadata layout** ([Faultlore](https://faultlore.com/blah/swift-abi/), [Swift.org library evolution](https://www.swift.org/blog/library-evolution/)). Resilient types hide layout behind runtime-queried value witness tables (size, alignment, stride, copy/move/destroy, extra inhabitants); `@frozen` types commit to layout permanently; reabstraction thunks bridge calling conventions.

Any implementation compiled from the same Swift version's sources exports the same mangled symbols and the same metadata shapes, because *it is the same source*. Build it with `-install_name /usr/lib/swift/libswiftCore.dylib` and `compatibility_version 1.0.0` and dyld will bind an Apple-built client's imports against it. Set `current_version` generously high â€” dyld enforces compatibility version, not current version.

**So the answer to "will it load" is yes.** The interesting question is fidelity, and there the honest answer is more textured.

### 3.2 Fidelity risk 1: version skew (the dominant one)

An Apple-built binary from a modern Xcode references stdlib symbols that **did not exist in 5.4**: typed throws support, `_StringProcessing`/`_RegexParser` entry points, newer `_swift_task_*`, newer `_StringGuts` internals, `Observation`. A 5.4-era `libswiftCore` will fail to bind these with `Symbol not found`.

**Recommendation: build the newest stdlib you can get to work, not the "matching" 5.4.** The ABI is forward-designed for exactly this â€” a newer stdlib is a superset that still satisfies older clients. The cost is that newer stdlibs assume newer objc4 features (relative method lists, `_objc_loadClassref`, `swift_updateClassMetadata2`) and newer dyld behavior, which may exceed what Darwin 20's objc4-818 provides. **This is a genuine tension and it deserves an explicit spike early in Phase S0**: pick a stdlib version by building 5.4, 5.8, and 5.10 and diffing their imports against OpenOSX's objc4/libSystem export set. That one experiment determines the ceiling of the whole workstream, and it is a few days of work.

### 3.3 Fidelity risk 2: inlined code assumes exact `@frozen` layouts

`@inlinable` and `@_alwaysEmitIntoClient` stdlib code is already compiled *into the Apple-built client*. That inlined code hardcodes the internal layout of `String`, `Array`, `Dictionary`, `Set`, `Int`, `Optional`. If your `libswiftCore` disagreed by one byte, you would get silent memory corruption rather than a clean link error.

In practice this is **safe, because you are compiling the same source**. But it is exactly why "write our own libswiftCore from scratch" is not a viable alternative strategy, and why any local patches to stdlib internals are radioactive. Rule to write down now: **OpenOSX's libswiftCore must be an unmodified upstream build.** Patch the build system, never the stdlib internals.

### 3.4 Fidelity risk 3: the availability/version wall

This one is under-appreciated and I want to flag it clearly, because it constrains the entire "run real macOS apps" goal, not just Swift.

A modern app carries `LC_BUILD_VERSION` with `platform=MACOS, minos=14.0`. OpenOSX reports Darwin 20.5 / macOS 11.4. Two failures follow:

1. dyld version-gates loading; a binary demanding a newer OS than the running system will not load cleanly ([Apple forums on `LC_BUILD_VERSION`/`vtool`](https://developer.apple.com/forums/thread/726412)).
2. Even if it loads, **every `if #available(macOS 13, *)` check in the binary takes the fallback path**, because availability keys off the reported OS version.

So "ship a perfect Swift runtime" does not by itself make modern apps run. At some point OpenOSX must decide to **report a higher product version** â€” and that is a project-wide commitment, because the moment you claim macOS 13, every app assumes the full macOS 13 API surface exists. `vtool` can rewrite an individual binary's minos as a per-app shim, which is probably the right near-term escape hatch (and is conceptually the same move Wine makes with per-app Windows version spoofing).

**This belongs in the "Wine for OpenOSX" design (task #12) as a first-class subsystem: a per-application compatibility profile that controls reported OS version.** It is not a Swift problem, but Swift is where you will first hit it.

### 3.5 Realistic fidelity verdict

| Workload | Verdict |
|---|---|
| Pure-Swift CLI, `import Swift` only | **High confidence.** Should work well once Phase S0 lands. |
| Swift CLI with `import Foundation` | **Blocked** on `libswiftFoundation` â†’ blocked on real ObjC Foundation. |
| Swift + `async/await`, macOS 11 target | **Plausible** â€” app back-deploys its own concurrency dylib. |
| Swift + concurrency, macOS 12+ target | Blocked on system concurrency runtime + libdispatch SPI. |
| Swift GUI (AppKit / SwiftUI) | Blocked on AppKit; SwiftUI is a separate and much larger problem than AppKit. |

Worth stating plainly: **SwiftUI is not on this roadmap at any horizon I would put a number on.** It is closed, enormous, deeply entangled with CoreAnimation and the Swift result-builder/property-wrapper machinery, and has no open reimplementation of consequence. Anyone reasoning about "running modern Mac apps" should assume AppKit apps are the target and SwiftUI apps are not.

---

## 4. Effort estimate

Engineer-months, assuming one competent engineer who knows Mach-O and build systems. Confidence noted because the spread is wide.

| Phase | Deliverable | Effort | Confidence | Depends on |
|---|---|---|---|---|
| **S0** | `libswiftCore` for `x86_64-apple-darwin20`; a hello-world Swift binary from a real Mac runs in OpenOSX | **1â€“2 mo** | Medium-high | Mac build node (task #14) |
| **S1** | `SwiftOnoneSupport`, `BackDeployConcurrency`, `RemoteMirror`; symbol audit vs objc4/libSystem | **0.5â€“1 mo** | High | S0 |
| **S2** | Platform overlays: `Darwin`, `ObjectiveC`, `Dispatch`, `os`, `simd`, `XPC`, `CoreFoundation` | **2â€“4 mo** | Medium | S1 + `.swiftinterface` technique (Â§5.2) |
| **S3** | `libswiftFoundation` | **12â€“36 mo** | **Low** | A real ObjC Foundation â€” *not currently on the roadmap at all* |
| **S4** | `libswiftAppKit`, `libswiftCoreGraphics`, `libswiftQuartzCore` | **6â€“12 mo** *after* AppKit | Low | AppKit (task #12) |

**Read the shape of that table, not the numbers.** S0+S1+S2 is ~4â€“7 months and delivers real capability. S3 and S4 are dominated by their non-Swift prerequisites. The Swift-specific increment on top of a hypothetical finished Foundation+AppKit is maybe **15â€“20%** â€” Swift is a tax on those projects, not a project of comparable size.

The corollary is the important part: **S3's true cost is the Foundation port, and OpenOSX has not scoped a Foundation port.** Today's Foundation is six files. If the "Wine for OpenOSX" goal is serious, a full ObjC Foundation is a mandatory, multi-engineer-year line item that currently appears nowhere in tasks #1â€“#14. I would treat surfacing that as one of the more valuable outputs of this research, independent of anything Swift-specific.

---

## 5. Recommended plan

### 5.1 Roadmap placement

**Do S0 now, in parallel with the desktop/graphics work. Defer S2. Do not schedule S3/S4 until their prerequisites are real.**

Reasoning:

- S0 is **independent** of graphics. It touches objc4, libSystem, dyld, and the cross toolchain â€” all things the project already owns. It does not compete for the same attention as XFCE/Mesa work.
- S0 **de-risks the objc4 and libSystem surface early**, and does so with a uniquely demanding client. The Swift runtime exercises corners of the ObjC runtime that ordinary ObjC code never touches. If OpenOSX's objc4 has gaps, `libswiftCore` will find them, and finding them now is much cheaper than finding them during AppKit bring-up.
- S0 has **outsized demonstration value** relative to cost: "OpenOSX runs an unmodified Apple-built Swift binary" is a genuinely striking claim, and it is the kind of concrete milestone this project has been good at hitting (M1â€“M4).
- Overlays (S2) are **wasted work until Foundation exists**, because `import Foundation` is the first line of nearly every real tool.

### 5.2 The `.swiftinterface` technique â€” the highest-leverage idea here

For every closed overlay, the macOS SDK ships **textual `.swiftinterface` files** at paths like `MacOSX.sdk/usr/lib/swift/Darwin.swiftmodule/x86_64-apple-macos.swiftinterface`. There is even a first-party tool, `swift-build-sdk-interfaces`, that batch-compiles every textual interface in an SDK into binary modules ([swift-driver README](https://github.com/swiftlang/swift-driver/blob/main/README.md)). Textual interfaces are *"a superset of Swift source code that can be distributed along with binary libraries."*

The technique:

1. Take the SDK's `.swiftinterface` for an overlay.
2. Compile it with `swiftc -emit-module -emit-library` against OpenOSX's own ObjC headers.
3. You get a dylib with the **correct exported symbol set, correct mangled names, and correct type metadata** â€” by construction, not by reverse engineering.
4. Fill in bodies: mostly thin thunks down to the ObjC/C API via the imported clang module; `@inlinable` bodies come free in the interface file itself; the remainder can `fatalError()` initially and be filled in as apps demand them.

This is the difference between *guessing* an overlay's ABI and *deriving* it. For thin overlays like `Darwin`, `ObjectiveC`, and `simd` â€” which are largely typealiases and trivial wrappers â€” it may get you most of the way in one pass.

**Legal caveat, and it is a real one.** `.swiftinterface` files are declaration-level artifacts and sit in the same category as the public SDK headers OpenOSX already legally builds against â€” they are the Swift analogue of a header. That is a reasonable argument. But they are **Apple SDK content under the Xcode/SDK license**, and a shipped dylib compiled from them is more plausibly "derived from Apple's interface files" than a binary compiled against a header is. I am not comfortable asserting this is clean, and it should not be treated as settled on my say-so.

Concrete guidance:

- **Build-time only, always.** Extend the existing `requireFile` pattern (`foundation.nix` already does exactly this for `MacOSX11.3.sdk.tar.xz`, which is correctly marked "proprietary â€” not fetchable/redistributable"). The SDK never enters the repo or CI cache.
- **Get the shipping question answered before writing overlay code**, not after. If the answer is no, the fallback is regenerating declarations from Apple's *public developer documentation* â€” slower, more error-prone on exact mangling, but unambiguously clean.
- **This does not taint anyone.** Reading declarations is categorically different from reading leaked implementation source. The ReactOS-style contamination risk is not in play here. Keep it that way by keeping the boundary bright: interfaces and public docs yes, implementation source never.

### 5.3 Concrete Phase S0 task list

1. Stand up the Mac SSH build node (task #14 â€” this is its first real customer).
2. **Spike: build stdlib at 5.4, 5.8, and 5.10** for `x86_64-apple-macos11.0`. Diff each one's undefined symbols against OpenOSX's `libobjc.A.dylib` + `libSystem.B.dylib` exports. **This experiment picks your stdlib version and sets the ceiling for everything downstream â€” do it before anything else.**
3. Install `libswiftCore.dylib` into the image at `/usr/lib/swift/` with `install_name /usr/lib/swift/libswiftCore.dylib`, `compatibility_version 1.0.0`, high `current_version`.
4. Add `nix/pkgs/apple/swift-core.nix` following the existing `libcxx-dylib.nix` / `libobjc.nix` conventions.
5. **Acceptance test:** compile a hello-world with the *Apple* toolchain on the Mac, copy the unmodified binary into the OpenOSX VM, run it. Add as a CI job alongside the existing three.
6. Add a `swift-test.nix` in the style of the existing `objc-test.nix` / `dlsym-test.nix` probes.

### 5.4 License summary

| Component | License | Shippable in OpenOSX? |
|---|---|---|
| `swiftlang/swift` (compiler, stdlib, runtime) | **Apache 2.0 + Runtime Library Exception** | **Yes** â€” the RLE exists precisely so runtime code can be linked into binaries without attribution burden. Clean fit with APSL/BSD. |
| `swift-corelibs-libdispatch` | Apache 2.0 | Yes |
| `swift-corelibs-foundation` | Apache 2.0 | Legally yes, **technically a trap** (Â§2.6) |
| `swift-foundation` (`FoundationEssentials`) | Apache 2.0 | Yes â€” best long-term option for a Foundation *implementation* |
| Apple SDK `.swiftinterface` | Proprietary | **Build-time only; legal review before shipping derived output** |
| `darlinghq/darling-swift` | Mixed; bundles **Apple-built binaries** | **Do not vendor.** Study only. Redistributing Apple binaries is a separate and worse problem than the GPL concern. |

Apache 2.0 obligations are mild but non-zero: preserve `NOTICE`, note modifications, and be aware of the patent-grant/termination clause. No conflict with APSL/BSD.

---

## 6. Key file references

- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\apple\foundation.nix` â€” the `requireFile` SDK pattern to reuse for Swift; also documents how small the current Foundation is (`NSString`, `NSCFString`, `NSArray`, `NSDictionary`, `NSURL`, `NSError`)
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\apple\libobjc.nix` â€” objc4; the Swift-interop hooks live here
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\apple\icucore.nix` â€” satisfies the pre-5.8 stdlib ICU dependency
- `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\nix\pkgs\apple\libcxx-dylib.nix`, `objc-test.nix`, `dlsym-test.nix` â€” conventions to follow for `swift-core.nix` and `swift-test.nix`

---

## 7. Three things I'd want the project to internalize

1. **The Darwin overlays being closed is the structural fact of this whole area.** Everything else follows from it. `libswiftCore` open + overlays closed means the core runtime is a solved problem and the useful surface is not.
2. **`libswiftFoundation` is a Foundation project, not a Swift project** â€” and OpenOSX has not scoped a Foundation port. That gap is larger than the Swift gap and currently invisible in the task list.
3. **The version/availability wall (Â§3.4) will bite the "run real macOS apps" goal regardless of Swift**, and wants a per-app compatibility-profile design in task #12 rather than an ad-hoc fix later.

---

## Sources

- [Evolving Swift On Apple Platforms After ABI Stability â€” Swift.org](https://www.swift.org/blog/abi-stability-and-apple/)
- [Library Evolution in Swift â€” Swift.org](https://www.swift.org/blog/library-evolution/)
- [ABI Stability and More â€” Swift.org](https://www.swift.org/blog/abi-stability-and-more/)
- [How Swift Achieved Dynamic Linking Where Rust Couldn't â€” Faultlore](https://faultlore.com/blah/swift-abi/)
- [Building CoreFoundation for Darwin Based OS's â€” Swift Forums (PureDarwin â†” Tony Parker)](https://forums.swift.org/t/building-corefoundation-for-darwin-based-oss/75378)
- [swiftlang/swift `stdlib/public` @ release/5.5 â€” GitHub contents API](https://github.com/swiftlang/swift/tree/release/5.5/stdlib/public)
- [swift GettingStarted.md @ release/5.5](https://github.com/swiftlang/swift/blob/release/5.5/docs/HowToGuides/GettingStarted.md)
- [swift-sdk-generator](https://github.com/apple/swift-sdk-generator/) Â· [SE-0387 Cross-Compilation Destinations](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0387-cross-compilation-destinations.md)
- [Swift 5.5.2 fails to link libswift_Concurrency.dylib â€” Swift Forums](https://forums.swift.org/t/swift-5-5-2-xcode-13-2-beta-fails-to-link-libswift-concurrency-dylib/53263)
- [Using async/await in a command line tool on older macOS â€” Nonstrict](https://nonstrict.eu/blog/2023/using-async-await-in-a-commandline-tool-on-older-macos-versions/)
- [Missing libraries in /usr/lib on Big Sur (dyld shared cache) â€” Apple Developer Forums](https://developer.apple.com/forums/thread/655588)
- [How to use vtool to change LC_VERSION_MIN_MACOSX / LC_BUILD_VERSION â€” Apple Developer Forums](https://developer.apple.com/forums/thread/726412)
- [Could not find or use auto-linked library swiftCompatibility50 â€” Swift Forums](https://forums.swift.org/t/could-not-find-or-use-auto-linked-library-swiftcompatibility50/54351)
- [swift PR #25473 â€” back deployment for dynamic-replacement runtime functions](https://github.com/swiftlang/swift/pull/25473/files)
- [swift-driver README â€” swift-build-sdk-interfaces](https://github.com/swiftlang/swift-driver/blob/main/README.md)
- [swift-corelibs-libdispatch README](https://github.com/swiftlang/swift-corelibs-libdispatch/blob/main/README.md) Â· [Swift overlay integration PR #43](https://github.com/apple/swift-corelibs-libdispatch/pull/43)
- [swift-corelibs-foundation](https://github.com/apple/swift-corelibs-foundation) Â· [Apple Announces Full Swift Rewrite of Foundation â€” InfoQ](https://www.infoq.com/news/2022/12/apple-swift-foundation-rewrite/)
- [Swift 5.8 Released â€” Swift.org (ICU dependency removal)](https://www.swift.org/blog/swift-5.8-released/)
- [macOS Package Installer â€” Swift.org](https://www.swift.org/install/macos/package_installer/) Â· [Building with nightly Swift toolchains â€” Ole Begemann](https://oleb.net/2024/swift-toolchains/)
- [Swift 5 for MacAdmins â€” Scripting OS X](https://scriptingosx.com/2019/04/swift-5-for-macadmins/)
- [darlinghq/darling](https://github.com/darlinghq/darling) Â· [darlinghq/darling-swift](https://github.com/darlinghq/darling-swift)

