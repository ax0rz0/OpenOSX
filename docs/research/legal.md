# Clean-Room Process and License Architecture for OpenOSX

**Verdict up front:** the technical plan (reimplementing AppKit/CoreGraphics/CoreAnimation/WindowServer on top of the existing Darwin userland) is legally shippable, and considerably *less* exposed than ReactOS ever was â€” because most of what OpenOSX already ships is Apple's own **published** source under APSL, not reverse-engineered material. The clean-room discipline only needs to apply to a well-bounded set of never-published frameworks. The two things that will actually bite you are (1) **license architecture** â€” specifically GPL/LGPL code being *linked into* APSL-derived components, and (2) **the name**. Both are fixable now and expensive to fix later.

---

## 1. What clean-room reverse engineering actually requires, and why leaked source is fatal

### 1.1 The legal substrate

Copyright protects **expression**, not **ideas, methods of operation, or functionality**. Every clean-room practice descends from that one sentence. The relevant precedents:

| Case | Holding that matters to OpenOSX |
|---|---|
| [NEC v. Intel (1989/90)](https://en.wikipedia.org/wiki/Clean-room_design) | First US acceptance of the clean-room defense. Similarity in routines forced by *compatibility requirements* was held to be functionally constrained and therefore "likely free of a creative element." This is the doctrinal core: **ABI-mandated similarity is not infringement.** |
| [Sega v. Accolade (9th Cir. 1993)](https://en.wikipedia.org/wiki/Sega_v._Accolade) | Intermediate copying during reverse engineering is fair use where it is necessary to reach unprotected functional elements and the protected material does not end up in the shipped product. |
| [Sony v. Connectix (9th Cir. 2000)](https://caselaw.findlaw.com/court/us-9th-circuit/1452245.html) | Explicitly blessed **disassembly of discrete portions** of the Sony BIOS as "necessary" intermediate copying. The Virtual Game Station was a legitimate, transformative, non-infringing end product. This is the closest analogue to what an OpenOSX compatibility layer does. |
| [Google v. Oracle (US 2021)](https://www.congress.gov/crs-product/LSB10597) | Copying ~11,500 lines of Java SE **declaring code** (API signatures) to reimplement an interface was fair use, 6â€“2. The Court assumed copyrightability arguendo and did not decide it. See Â§3. |
| [SAS Institute v. World Programming (CJEU C-406/10)](https://en.wikipedia.org/wiki/SAS_Institute_Inc_v_World_Programming_Ltd) | EU: copyright does **not** protect a program's functionality, its programming language, or its file formats. Studying, observing and testing a lawfully-obtained program to reproduce its functionality is not infringement. |
| [EU Software Directive 2009/24/EC](https://eur-lex.europa.eu/legal-content/EN/TXT/PDF/?uri=CELEX:32009L0024) Art. 5(3) & Art. 6 | Art. 5(3) permits observation/study/testing without authorisation. Art. 6 permits **decompilation** where indispensable to achieve interoperability of an independently created program â€” with conditions (only the necessary parts, info not otherwise available, not used for a substantially similar competing program). Non-waivable by contract. |
| [17 U.S.C. Â§1201(f)](https://www.eff.org/issues/coders/reverse-engineering-faq) | DMCA anti-circumvention carve-out for identifying/analysing elements necessary to achieve interoperability of an *independently created* program. Relevant if you ever touch code-signing or DRM-adjacent paths. |

So reverse engineering per se is legal on both sides of the Atlantic when the target is interoperability. **Clean-room procedure is not what makes RE legal â€” it is what makes your legality *provable*.**

### 1.2 Why reading leaked proprietary source is categorically different

This trips people up, so state it precisely. Leaked source is not "extra-spicy disassembly." It fails on **four independent axes**, and only the first is a copyright question:

1. **Copyright â€” derivative work.** Code written by someone who has read the original is presumptively derived from it. There is no fair-use shelter here: nothing about reading leaked source is "necessary to achieve interoperability," because you never lawfully obtained a copy in the first place. Sega/Connectix protect *intermediate copies of lawfully acquired products*. A leak is neither.

2. **Trade secret â€” this is the real killer, and it is not curable by rewriting.** Unpublished Apple framework source is a trade secret. Misappropriation liability attaches to *acquisition* by improper means and to *use*, independent of copyright. A perfectly rewritten, non-substantially-similar implementation can still be an unlawful use of a misappropriated secret. Clean-room rewriting cures copyright derivation; it does **not** cure trade secret taint. This is why "just rewrite it" is not an answer.

3. **Evidence â€” burden shifting.** Copyright infringement is proven by *access* + *substantial similarity*. Ordinarily a plaintiff has to prove access. A contributor who has publicly read leaked source hands them access for free, and then every ABI-forced similarity â€” which, per NEC v. Intel, is exactly what a compatible implementation is *made of* â€” starts looking like copying. You lose the NEC defense precisely because you can no longer claim independence.

4. **Contract/NDA.** For ex-Apple engineers, employment agreements and NDAs bind regardless of copyright law.

### 1.3 The three project precedents

**ReactOS â€” the cautionary tale.** In January 2006, then-developer Hartmut Birr alleged on ros-dev that ReactOS contained code derived from disassembling Windows. The project [froze contributions, pulled downloads, and audited ~3 million lines](https://www.linux.com/news/reactos-suspends-development-source-code-review/). The finding was that nothing had been *pasted* from Windows source, but that some developers had had access and used it "as inspiration"; such code was to be marked **dangerous** and re-audited by a **clean** person. Notably, [ReactOS did not ban tainted developers](https://www.theregister.com/2019/07/03/reactos_windows_research_kernel_claim/) â€” and that decision kept the accusation alive. In 2019 a Microsoft kernel engineer publicly claimed ReactOS derived from the Windows Research Kernel, citing matching internal structure names, unexported function names, and identical macro conventions: *"there is absolutely no way on Earth this was written from a clean sheet only from the available public documentation."*

The lessons, in order of importance:
- **The cost was never a lawsuit. Microsoft never sued.** The cost was a multi-year loss of momentum and a permanent reputational asterisk. That is the realistic downside for OpenOSX too.
- **Retrospective auditing barely works.** You cannot reconstruct provenance for code committed years ago by contributors you can't reach. Provenance must be captured *at commit time* or it does not exist.
- **Internal names are the fingerprint.** Unexported function names, internal struct member names, and macro naming conventions are what convinced an outside expert. Public symbols are ABI facts; private symbols are evidence of having seen the source.

**Wine â€” the strict-abstinence model.** [Wine has not allowed reverse-engineered (disassembly-derived) code from the start; if a developer is caught, their code is removed.](https://forum.winehq.org/viewtopic.php?t=7138&start=25) What Wine *does* permit is **black-box testing**: running Microsoft DLLs in a contained environment and observing behaviour without disassembling them, plus **substitution testing** â€” swapping in your own implementation of what you believe the black box does and verifying that dependent software still works. Two things worth internalising:
- Wine's policy is **stricter than the law requires**. Sony v. Connectix says disassembly can be fair use. Wine forbids it anyway, because the policy's job is deterrence and provability, not staying barely inside the line.
- Wine's own forums make an underrated observation: *lack of errors is a giveaway of reverse-engineered code* â€” clean-room code shows a development cycle, whereas magic constants and fully-correct-first-try edge cases appear from nowhere. **Your messy commit history is exculpatory evidence.** Do not squash it away.

**GNUstep â€” the published-spec model.** GNUstep's defensible position rests on implementing to the **published OpenStep specification**, a real public document. Its rules are narrow and enforceable: [*"PLEASE, do not copy from the Apple documentation or any other copyrighted documentation"*](https://mediawiki.gnustep.org/index.php/Developer_FAQ), and everyone contributing more than ~20 lines signs an FSF copyright assignment, which the project describes as making it "easier to defend against any copyright infringement suits." Note what GNUstep does *not* claim: it does not claim its contributors have never used a Mac. It claims they never copied expression.

**The classic two-team model**, for reference: team A examines the target and writes a specification; a legal/clean reviewer scrubs the spec of anything expressive; team B, which has never seen the target, implements from the scrubbed spec alone. [The "clean room" metaphor is about the *implementers'* environment being demonstrably uncontaminated.](https://en.wikipedia.org/wiki/Clean-room_design)

### 1.4 How this scopes down for OpenOSX specifically

This is the most useful thing in this report and it is easy to miss. **OpenOSX is not ReactOS.** ReactOS had to clean-room *everything*, because Microsoft published nothing. OpenOSX's situation is stratified:

| Layer | Status | Clean-room needed? |
|---|---|---|
| XNU 20.5, libSystem, dyld, objc4, CoreFoundation 1338, libc++, Security, IOKit, launchd, mDNSResponder | **Published by Apple under APSL 2.0 / BSD.** Lawfully licensed source. | **No.** This is licensed use, not RE. Keep APSL notices intact (your `docs/PUREDARWIN_ATTRIBUTION.md` already gets this right). |
| Foundation, CoreServices, XPC, DiskArbitration, SystemConfiguration | Mostly published; verify per-component | No, where published |
| **AppKit, CoreGraphics/Quartz, CoreAnimation, WindowServer/CGS, Metal, AVFoundation, CoreAudio, Swift runtime** | **Never published.** | **Yes â€” this is the entire clean-room perimeter.** |

Your policy therefore does not need to be a project-wide priesthood. It needs to be a **strict regime over one directory subtree** (the new frameworks) and a light attestation everywhere else. That is achievable by a hobby project. A project-wide ReactOS-style regime is not.

Second scoping point: the Swift runtime is **open source under Apache 2.0** ([swift.org/legal](https://www.swift.org/legal/license.html)). It does not belong in the clean-room bucket at all â€” it's a porting job, not a reimplementation job.

---

## 2. A concrete contributor policy for OpenOSX

Below is a policy sized for a small project. The design goal is: **every rule must be checkable by one person in under a minute, or it will not be followed.**

### 2.1 The three-tier source classification

Put this verbatim in `docs/CLEANROOM_POLICY.md`.

**Tier A â€” GREEN. Always permitted, no special handling.**
- Apple's published open source (opensource.apple.com, github.com/apple, swiftlang) under APSL/BSD/Apache. This is licensed code, not RE.
- Apple's public developer documentation at developer.apple.com â€” as a **source of facts**, not of text. Read it, then write your own declarations and your own prose. Never paste.
- Public SDK headers (see Â§3 for the important caveats about *using* vs *shipping*).
- Published Apple technical notes, WWDC session content, the archived Aqua/macOS Human Interface Guidelines, `man` pages.
- Third-party published RE write-ups, conference talks, blog posts, books (Levin's *\*OS Internals* series, etc.).
- Open-source reimplementations under compatible licenses (see Â§4).
- **Observed behaviour** of Apple binaries you lawfully possess on hardware you lawfully own: `nm`/`otool -L`/`class-dump`-style symbol and selector enumeration, `dtrace`, `lldb` breakpoints, Mach message tracing, framework `Info.plist` contents, exported symbol lists, struct sizes inferred by `sizeof` probes, error codes and their triggering conditions.

**Tier B â€” AMBER. Permitted, but requires the two-role split and explicit provenance.**
- Static disassembly / decompiler output (Ghidra, IDA, Hopper) of Apple binaries.

Legally this is likely fine under Connectix and Art. 6. Procedurally it is where ReactOS died. So: **the person who disassembles must not be the person who implements that function.** The disassembler writes a specification in `docs/spec/<Framework>/<symbol>.md` describing *what the function does* in prose â€” parameters, semantics, ordering, error conditions, observable side effects â€” containing **no pseudocode, no variable names, no control-flow transcription, no register-level detail**. The implementer works from the spec only. On a two-person project this is workable; on a one-person project, Tier B is simply closed to you and you must stay in Tier A. Say that out loud rather than pretending.

**Tier C â€” RED. Absolutely forbidden. Contribution-disqualifying.**
- Any non-public Apple source code, however obtained: the various iOS/macOS source leaks, internal Apple headers, `.tbd`/internal SDK trees not publicly released, GitHub mirrors of leaked trees, screenshots or excerpts of leaked source posted anywhere.
- Code obtained under NDA, or knowledge obtained as an Apple employee/contractor subject to confidentiality.
- **A contributor who has read any of the above must not contribute to the clean-room subtree at all.** They remain welcome everywhere else â€” packaging, CI, the XNU/userland side, the desktop, docs, testing. Unlike ReactOS, draw this line explicitly and in advance. It costs you almost nothing and it is the single answer to the accusation that permanently dogged ReactOS.

### 2.2 The interface/expression line â€” the rule contributors will actually need

The recurring practical question is "I can see this symbol in the binary â€” may I use it?" The distinction is between **interface facts** (needed for interoperability, functionally constrained, protected by NEC/Google v. Oracle) and **expression** (how Apple chose to implement it).

**Interface facts â€” permitted, record the source:**
- Exported symbol names, Objective-C class names, selector names, protocol names
- Method signatures and type encodings, ivar names and offsets *of public classes*
- Struct layouts, field offsets, alignment, enum/constant values, bitmask semantics
- Notification names, UserDefaults keys, plist keys, Mach message IDs, mig subsystem numbers
- Framework install names and dylib load paths (functionally mandatory â€” see Â§5.3)
- Which selectors an app calls, in what order, with what arguments

**Expression â€” forbidden regardless of how you learned it:**
- Algorithms and their internal structure as Apple implemented them
- Internal (unexported/private) function names, internal struct member names of private types, macro naming conventions â€” *this is precisely the ReactOS 2019 fingerprint*
- Comments, string literals not part of the ABI, magic constants whose derivation you cannot independently explain
- Code layout, helper decomposition, error-handling structure

**The bright-line test to hand contributors:** *"If a competent engineer with only the public docs and observed behaviour would have to write it this way for the thing to work, it's an interface fact. If they could reasonably have written it a different way and it would still work, and yours matches Apple's, explain why."*

### 2.3 Per-file provenance

One header block, mandatory in the clean-room subtree. Machine-greppable.

```
// SPDX-License-Identifier: APSL-2.0
// OpenOSX-Provenance: tier-a
// OpenOSX-Sources:
//   - https://developer.apple.com/documentation/appkit/nsview  (fetched 2026-08-07)
//   - observed: -[NSView drawRect:] call ordering, macOS 11.4, lldb trace
//   - Cocotron NSView.m (MIT) â€” adapted, see THIRD_PARTY_LICENSES.md
// OpenOSX-Author-Attestation: no non-public Apple source consulted
```

For Tier B files, add `OpenOSX-Spec: docs/spec/CoreGraphics/CGContextDrawPath.md` and `OpenOSX-Spec-Author: <person>` â€” and CI enforces that `Spec-Author` â‰  commit author.

### 2.4 The defensible record â€” five artifacts, that's all

1. **`docs/CLEANROOM_POLICY.md`** â€” the policy above. Dated. Version-controlled. The fact that it *predates* the code is itself the evidence.
2. **`CONTRIBUTING.md` with a DCO-plus clause.** Extend the standard Developer Certificate of Origin with one project-specific line requiring `Signed-off-by:` on every commit:
   > *"I certify that I have not consulted any non-public Apple source code, internal documentation, or material obtained under confidentiality in producing this contribution, and that its provenance header is accurate."*
   
   This is the highest-value, lowest-effort item on the list. It creates a per-commit, timestamped, attributed record. It is what ReactOS wishes it had had in 2004.
3. **`docs/spec/`** â€” the written specifications for Tier B work. These are the clean-room's physical evidence.
4. **`THIRD_PARTY_LICENSES.md`** â€” every vendored component, its license, its upstream commit hash, and what you changed. Extend the pattern you already use in `docs/PUREDARWIN_ATTRIBUTION.md`, which is a genuinely good model.
5. **Unsquashed history.** Per the Wine observation: a commit trail showing wrong guesses, fixed bugs, and iteration is affirmative evidence of independent creation. Preserve it.

### 2.5 Quarantine procedure â€” write it *before* you need it

If a taint allegation ever lands: (a) identify affected files by provenance header, not by memory; (b) mark them `OpenOSX-Provenance: quarantined` and exclude from builds â€” do **not** freeze the whole project, which is what cost ReactOS its 0.3.x momentum; (c) rewrite from spec by a clean author; (d) publish the audit result. Bounded, per-file, keeps the project alive. Having this written down in advance converts a crisis into a checklist.

### 2.6 What *not* to do (anti-corporate-theater)

- Don't require lawyer review of every spec. Require that specs contain no pseudocode; that's checkable by eye.
- Don't require contributors to prove a negative about their entire career. One attestation about *this contribution*.
- Don't ban owning a Mac, running macOS, or reading Apple's docs. Those are Tier A and are the whole point.
- Don't ban `class-dump` output or symbol tables. Those are interface facts and you cannot build a compatibility layer without them.

---

## 3. Are public SDK headers safe to implement against?

**Short answer: yes to implement against; be careful about *shipping* them; prefer APSL headers wherever they exist.** Three separate questions get conflated here.

### 3.1 Copyright â€” Google v. Oracle

[The Supreme Court held 6â€“2 that Google's copying of Java SE's declaring code was fair use](https://www.congress.gov/crs-product/LSB10597), characterising it as "reimplementation of a user interface," with all four statutory factors favouring Google. Critically, the Court **assumed copyrightability arguendo and did not decide it** â€” so "API declarations aren't copyrightable" remains unestablished. What you get is a fair-use holding, not a categorical safe harbour.

But the reasoning maps onto OpenOSX unusually well. Google copied declarations verbatim *because* they had to for existing Java developers' knowledge and code to carry over. OpenOSX needs `@interface NSView : NSResponder` and `-[NSView setNeedsDisplay:]` for existing Mach-O binaries to link and run at all â€” a stronger interoperability necessity than Google's. And the transformative-use analysis favours a compatibility layer on a different OS even more than it favoured Android.

Add to that: declarations have near-zero expressive latitude. `CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat width, CGFloat height);` cannot be written differently and still work. That is the NEC v. Intel functional-constraint argument, and in the EU, [SAS v. WPL](https://en.wikipedia.org/wiki/SAS_Institute_Inc_v_World_Programming_Ltd) puts interfaces, languages and formats outside protection more cleanly than US law does.

**Practical rule: re-declare, don't paste.** Write the declarations yourself from the documentation. Do not copy doc comments, `NS_SWIFT_NAME` annotations, availability macros verbatim, or Apple's header prose. The declarations will converge (they must); the surrounding material should not.

### 3.2 Contract â€” the Xcode SLA is the sharper edge

Copyright is the *weaker* constraint here. The [Xcode and Apple SDKs Agreement](https://www.apple.com/legal/sla/docs/xcode.pdf) is a contract, and it contains terms that copyright law would not impose:

- **Apple-branded-hardware restriction.** You may not install, use, or run the Apple SDKs on non-Apple-branded computers. Your `README.md` already says "OpenOSX builds only on macOS," and your task list mentions wiring a Mac as an SSH build node â€” so you're already compliant, but it's worth knowing *why* this matters rather than treating it as an accident.
- **Anti-RE clause.** Prohibits decompilation, reverse engineering, disassembly, and deriving source â€” *"except to the extent prohibited by applicable law or permitted by open-source licensing terms."* That carve-out is load-bearing: EU Software Directive Art. 6 rights are **non-waivable by contract**, so an EU-based contributor retains decompilation-for-interoperability rights regardless of the SLA. A US contributor has weaker footing, since US courts have been more willing to enforce contractual RE bans.
- **No redistribution.** Nothing in the SLA lets you ship Apple's SDK headers.

### 3.3 The recommendation that actually matters

**Establish a header-source hierarchy and follow it strictly:**

1. **APSL/BSD headers from opensource.apple.com** â€” for CoreFoundation, objc4, libSystem, IOKit, Security, etc. These come with an actual redistribution licence. **You may vendor these.** This is what OpenOSX already does, and it is the strongest position available. Use it for everything it covers.
2. **Re-declared from public documentation** â€” for AppKit, CoreGraphics, CoreAnimation, Metal, AVFoundation. No APSL source exists; write the headers yourself from developer.apple.com plus observed symbol/selector data. These are **your** headers, under **your** licence, and you may ship them.
3. **Xcode SDK headers** â€” use only transiently on an Apple-branded Mac for cross-checking a signature. **Never vendor into the repo. Never ship in the image.**

Add a CI check that greps the tree for Apple SDK header fingerprints (`__OSX_AVAILABLE_STARTING`, `NS_ASSUME_NONNULL_BEGIN` in combination with Apple copyright banners) outside the APSL-sourced directories. Five lines of shell, catches the one mistake that would actually hurt.

---

## 4. License compatibility matrix

### 4.1 The one distinction that governs everything: aggregation vs. combination

Nearly all license panic in OS projects comes from missing this. **Shipping GPL software in the same disk image as APSL software is fine** â€” that's *mere aggregation*, explicitly permitted, and it's why every Linux distro can ship GPL, BSD, MPL and proprietary firmware on one ISO. OpenOSX already ships XFCE (GPL-2.0), GTK3 (LGPL-2.1), Mesa (MIT) and Xorg (MIT) alongside APSL components with no issue.

What matters is **combination into a single work** â€” static linking, source vendoring, deriving one component from another. The line runs at the linking boundary, not the filesystem boundary.

The critical fact for OpenOSX: **[APSL 2.0 is FSF-approved as a free software licence but is GPL-incompatible](https://www.gnu.org/philosophy/apsl.en.html)** â€” its partial copyleft cannot be reconciled with the GPL's terms. So: *no GPL code may ever be linked into, or vendored into, an APSL-derived component.* That is the single hard constraint in your license architecture.

### 4.2 The matrix

| Project | License | Vendor into shippable framework? | Dynamic link? | Static link? | Study only? |
|---|---|---|---|---|---|
| **Cocotron** | MIT | âœ… **YES â€” best-in-class** | âœ… | âœ… | â€” |
| **Skia** | BSD-3-Clause | âœ… **YES** | âœ… | âœ… | â€” |
| **swift-corelibs** (Foundation, libdispatch, XCTest) | Apache-2.0 + Runtime Library Exception | âœ… **YES** | âœ… | âœ… | â€” |
| **Swift runtime / compiler** | Apache-2.0 + RLE | âœ… **YES** | âœ… | âœ… | â€” |
| **Cairo** | LGPL-2.1 **OR** MPL-1.1 (dual, your choice) | âš ï¸ Only under MPL-1.1 option, file-level copyleft applies | âœ… | âš ï¸ under MPL only | â€” |
| **GNUstep** (base, gui, back) | LGPL-2.1+ | âŒ **NO** | âœ… **yes, with conditions** | âŒ practically no | â€” |
| **GNUstep-make** | GPL-3.0 | âŒ | âŒ | âŒ | build-time only |
| **WebKit** (WebCore, JavaScriptCore) | LGPL-2.1 | âŒ **NO** | âœ… | âŒ | â€” |
| **WebKit** (Apple-contributed portions) | BSD-2-Clause | âœ… those files only | âœ… | âœ… | â€” |
| **Darling** (core, corecrypto, most of tree) | GPL-3.0 | âŒ **STUDY ONLY** | âŒ | âŒ | âœ… |
| **darling-cocotron** (MIT-origin files) | MIT (per-file â€” **verify headers**) | âœ… those files only | âœ… | âœ… | â€” |
| **GTK3 / GLib** | LGPL-2.1 | âŒ | âœ… | âŒ | â€” |
| **XFCE** (xfwm4, panel, thunar, xfdesktop) | GPL-2.0 | âŒ into APSL components | separate processes = fine | âŒ | â€” |
| **Mesa, Xorg, wlroots** | MIT / MIT-ish | âœ… | âœ… | âœ… | â€” |

### 4.3 Notes on the entries that need judgement

**Cocotron (MIT) â€” your best asset, and you should plan around it.** [MIT, clean-room partial reimplementation of Foundation and AppKit](https://www.cocotron.org/), cross-platform, with [an adequately complete AppKit](https://github.com/darlinghq/darling/issues/39). MIT is *fully* compatible with an APSL/BSD image: vendor it, relicense derivatives, no reciprocal obligations, just preserve the notice. Two caveats: it targets the Cocoa API of roughly the 10.5â€“10.6 era, so it gives you a skeleton rather than a modern AppKit; and it has [significant fork fragmentation](https://cocoadev.github.io/CocoTron/) â€” pick a base commit, record it in `THIRD_PARTY_LICENSES.md`, and treat forks as patch sources. **Note also the provenance bonus:** Cocotron is itself documented as a clean-room reimplementation, so vendoring it inherits a clean chain rather than muddying yours.

**Darling (GPL-3.0) â€” study only, and this is not a close call.** [Darling is GPL-3.0 with per-submodule variation](https://github.com/darlinghq/darling). GPL-3.0 + APSL-2.0 in one linked work is impossible in both directions. You may read it, learn from its *approach*, and cite its published write-ups â€” but **do not copy code, and be careful that "learning from its approach" doesn't become copying structure.** The one real exception: `darling-cocotron` files that retain [Cocotron MIT headers](https://github.com/darlinghq/darling-cocotron/blob/master/AppKit/NSControl.m) remain MIT and are usable â€” but check each file's header individually, because Darling's own additions to those files may be GPL. This is exactly the kind of thing that needs a per-file record in `THIRD_PARTY_LICENSES.md`.

**GNUstep (LGPL-2.1) â€” the honest analysis.** LGPL permits dynamic linking from differently-licensed code; the [FSF and general practice treat a dynamically-linked `.so`/`.dylib` as a "work that uses the Library" rather than a derivative work](https://en.wikipedia.org/wiki/GNU_Lesser_General_Public_License). But the LGPL's real obligation is the **relink requirement**: recipients must be able to modify the library and relink. In an *OS image*, this is not the embedded-device nightmare it usually is â€” you're shipping dynamic frameworks in a normal filesystem, so shipping the LGPL source and letting users drop in a replacement `.dylib` satisfies it naturally. Two conditions to honour: **(a) don't static-link it**, and **(b) don't let LGPL code become an unavoidable, unreplaceable core** â€” if `AppKit.framework` *is* GNUstep, every downstream integrator inherits the obligation.

My recommendation: **Cocotron as the vendored base, GNUstep as a studied reference and a possible optional dynamically-linked component â€” not as the core.** MIT into an APSL/BSD image is architecturally clean; LGPL at the heart of your primary framework creates a permanent constraint on a project whose whole premise is "everything we ship is built from open source" with maximal downstream freedom.

**Skia (BSD-3) vs Cairo (LGPL/MPL) for the CoreGraphics backend.** [Skia is New BSD](https://en.wikipedia.org/wiki/Skia_Graphics_Engine) â€” vendor it, static-link it, done. [Cairo is dual LGPL-2.1 / MPL-1.1 at your option](https://www.cairographics.org/); the MPL-1.1 option gives file-level copyleft (changes to Cairo files must be published; your own files stay yours) which is workable but strictly more constrained. Skia is also the better technical match for Quartz's model (paths, GPU acceleration, colour management, `SkCanvas` â‰ˆ `CGContext`). **Recommendation: Skia.**

**WebKit â€” dynamic-link only.** [WebCore and JavaScriptCore are LGPL-2.1; Apple's added portions are BSD-2](https://webkit.org/licensing-webkit/). Shipping WebKit as a standalone dynamic framework in the image is fine. Static-linking it into an APSL framework is not.

**swift-corelibs (Apache-2.0 + Runtime Library Exception) â€” unambiguously vendorable.** [The RLE removes the attribution requirement for embedded runtime portions](https://www.swift.org/LICENSE.txt). One footnote: Apache-2.0 is incompatible with GPL-**2**-only (patent clause), which matters if you ever tried to link Swift runtime code into an XFCE-derived GPL-2.0 component. Separate processes and separate dylibs â€” mere aggregation â€” are unaffected.

### 4.4 Two things clean rooms and licenses don't fix

- **Patents.** [Independent invention is not a defence to patent infringement](https://en.wikipedia.org/wiki/Clean-room_design), so no amount of clean-room discipline touches patent exposure. Realistically low priority for a hobby project (Apple does not sue hobby OS projects over UI patents, and much of the relevant art is aging out), but don't let anyone tell you the clean room covers it.
- **Design patents / trade dress** on UI elements â€” see Â§5.

---

## 5. Trademark reality check

### 5.1 The verified facts

I checked [Apple's official trademark list](https://www.apple.com/legal/intellectual-property/trademark/appletmlist.html). The following are **registered Apple trademarks**:

- **AquaÂ®** â€” "user interface"
- **OS XÂ®** â€” "operating system software"
- **CocoaÂ®** â€” "software technology"
- **QuartzÂ®** â€” "graphics and display technology"
- **FinderÂ®** â€” "operating system software"
- **SpotlightÂ®** â€” "software utility"
- **Time MachineÂ®** â€” "application program"

("Mac OS" and "Dock" did not appear on the list.) I did not check AppKit, Metal, or Core Animation â€” do that before using any of them as user-facing branding.

**So: "Aqua" is not available as a name for your theme.** Not "Aqua," not "OpenAqua," not "Aqua-compatible" as a product name.

[Apple's third-party guidelines](https://www.apple.com/legal/intellectual-property/guidelinesfor3rdparties.html) are unusually explicit:
- You may use Apple word marks in **referential compatibility phrases** â€” "runs on," "for use with," "compatible with" â€” provided *"The Apple word mark is not part of the product name,"* it *"appears less prominent than the product name,"* the claim is true, and it doesn't *"create a sense of endorsement, sponsorship, or false association."*
- You may **not** use or register an Apple trademark, in whole or in part, as a company, product, or service name.
- You may **not** use *"a variation, phonetic equivalent, foreign language equivalent, takeoff, or abbreviation of an Apple trademark."*
- You may **not** *"imitate the distinctive Apple packaging, web site design, logos, or typefaces."*

These are Apple's stated positions, not law â€” but they're the standard a cease-and-desist would be written against, and the "variation/abbreviation" clause is aimed squarely at names like yours.

### 5.2 The name â€” and the risk you may not have priced in

"OpenOSX" has **two** problems, and the second is more likely to reach you first.

**Problem 1: "OSX" reads as an abbreviation of OS XÂ®**, in the identical goods class (operating system software). Apple's guidelines call out abbreviations and variations explicitly. The [Microsoft v. Lindows](https://en.wikipedia.org/wiki/Microsoft_Corp._v._Lindows.com,_Inc.) precedent is instructive but cuts *against* you: Lindows survived US litigation only because a federal judge questioned whether "Windows" was generic â€” Microsoft [ultimately paid $20M to settle and Lindows renamed to Linspire](https://www.computerworld.com/article/1464816/microsoft-to-pay-20m-to-end-lindows-trademark-battle.html), having already lost European injunctions and burned two and a half years. **"OS X" is a coined mark with no genericness argument available.** You would not have Lindows's defence.

**Problem 2 â€” the one that will likely land first: there is a senior commercial user.** [openosx.com is an existing company selling Mac software under the OpenOSX name](https://openosx.com/) â€” Office, GrassPro, WinTel â€” and [asserts OpenOSX as its trademark](https://www.zoominfo.com/c/openosx-inc/147292688). Same name, same field, prior use. That's a textbook likelihood-of-confusion setup, and a small company defending a niche mark is *far more likely* to send you a letter than Apple is. There is also an unrelated [LibreCorp/OpenOSX on GitHub](https://github.com/LibreCorp/OpenOSX), which adds discoverability confusion.

**Recommendation: rename, and do it now.** You are pre-1.0, pre-public-release. The rename is cheap today â€” you have already proven you can execute a deep rename once (`docs/PUREDARWIN_ATTRIBUTION.md` documents exactly that process, and `git grep -i` plus the CMake/bundle-ID/macro conventions are all in place). It will not be cheap after a public launch, a user base, and a package ecosystem.

Pick something that evokes the *lineage* rather than the *trademark*. Darwin-derived, NeXT-derived, or wholly original all work; Darwin itself is not on Apple's trademark list, and the ecosystem precedent (OpenDarwin, PureDarwin) is well-established and unchallenged. Avoid "OS X," "OSX," "Mac," "Aqua," "Cocoa," "Quartz," "Finder" and phonetic near-misses of any of them.

### 5.3 Where Apple names are legitimately unavoidable â€” and why that's fine

Binary compatibility **requires** using Apple's names in specific functional positions:

- Framework install names: `/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit`
- Class and selector names: `NSWindow`, `-[NSView drawRect:]`
- Bundle identifiers referenced by third-party binaries, notification names, plist keys

This is **functional use, not brand use** â€” the strings are ABI, and a binary will not link without them. Wine does exactly this with `kernel32.dll` and `user32.dll` and has for thirty years. Keep the distinction crisp and defensible with two rules:

1. **Internal/on-disk names may match Apple's exactly** where the ABI requires it. Document this in `docs/CLEANROOM_POLICY.md` as an interoperability necessity, so the reasoning is on record.
2. **User-facing names must be yours.** The installer, the boot screen, the "About this computer" panel, the theme, the settings app, the marketing site â€” all original. A user should never see an Apple trademark presented as *your* branding.

### 5.4 Trade dress, artwork, and fonts â€” practical mitigations

Trademark is the smaller risk here; **copyright in artwork and trade dress in the overall look** are the bigger ones, and they're also the easiest to avoid.

- **Ship zero Apple assets.** No `.icns` files, no cursors, no wallpapers, no sounds, no `.car` asset catalogs, no menu-bar glyphs. Ever. This should be a CI check on the image, not a policy people remember.
- **Fonts are a trap.** Apple's system fonts (San Francisco, New York) ship under a licence limited to designing for Apple platforms â€” verify the current terms before touching them, but the safe answer is: **don't**. Lucida Grande and Helvetica Neue are third-party licensed and not yours to ship either. Use Inter, Public Sans, Source Sans, or commission something. This is one of the most common ways hobby "Mac-like" projects acquire a real legal problem for zero benefit.
- **Name your theme something original**, and design it as a *sibling* of Aqua rather than a clone. Aim for "clearly its own thing, obviously in the same tradition" â€” the way GNOME's Adwaita and elementary's Pantheon relate to their influences. Pixel-identical recreation of Apple's traffic lights, Dock magnification, and menu bar is where trade dress exposure starts.
- **Compatibility claims: use referential phrasing only.** "OpenOSX can run software built for macOS" âœ…. "OpenOSX for Mac OS X" âŒ. Never imply endorsement, sponsorship, certification, or affiliation.
- **Ship a disclaimer** in README, About box, and website footer: *"[Name] is an independent open-source project. It is not affiliated with, endorsed by, or sponsored by Apple Inc. Apple, macOS, OS X, Aqua, Cocoa, and Quartz are trademarks of Apple Inc., used here only to describe compatibility."* Costs nothing, negates the false-association element, and demonstrably good faith.
- **Never distribute Apple binaries.** Your README's "everything we ship is built from open source; no closed Apple binaries anywhere in the boot chain" is not just a technical principle â€” it's a legal moat. Guard it. If the compatibility layer ever needs a real Apple framework for testing, the *user* supplies it from their own licensed Mac; you ship nothing.

---

## 6. Action checklist

Ordered by (value Ã· effort). Items 1â€“4 are an afternoon's work and remove most of the long-term risk.

| # | Action | Where |
|---|---|---|
| 1 | **Rename the project.** Two independent trademark exposures; cost only rises from here. | repo-wide, using the `docs/PUREDARWIN_ATTRIBUTION.md` methodology |
| 2 | Write `CONTRIBUTING.md` with the DCO + no-non-public-Apple-source attestation; require `Signed-off-by:` | `CONTRIBUTING.md` |
| 3 | Write `docs/CLEANROOM_POLICY.md` â€” the three tiers, the interface/expression line, the quarantine procedure | `docs/CLEANROOM_POLICY.md` |
| 4 | Add the trademark disclaimer to README / About / site | `README.md` |
| 5 | Create `THIRD_PARTY_LICENSES.md` with upstream commit hashes and per-file license notes (Darling/Cocotron especially) | root |
| 6 | Adopt the provenance header block; add a CI check that clean-room-subtree files carry one | `.github/workflows/` |
| 7 | Add a CI grep for Apple SDK header fingerprints outside APSL-sourced directories | `.github/workflows/` |
| 8 | Decide the architecture: **Cocotron (MIT) vendored as the AppKit base + Skia (BSD) as the CoreGraphics backend**; GNUstep studied, optionally dynamically linked, never core | `docs/ROADMAP.md` |
| 9 | Create `docs/spec/` and commit the first specification before the first clean-room line of code | `docs/spec/` |
| 10 | Add an image-level CI check for Apple-origin assets (icons, fonts, sounds) | `.github/workflows/` |
| 11 | Name the theme; commission or draw original artwork; pick an open font | design |
| 12 | Confirm all SDK-touching builds run on Apple-branded hardware (your Mac SSH build node, task #14) | CI config |

---

## Sources

Clean-room doctrine and precedent: [Clean-room design](https://en.wikipedia.org/wiki/Clean-room_design) Â· [Sega v. Accolade](https://en.wikipedia.org/wiki/Sega_v._Accolade) Â· [Sony v. Connectix (9th Cir.)](https://caselaw.findlaw.com/court/us-9th-circuit/1452245.html) Â· [Sony v. Connectix (Justia)](https://law.justia.com/cases/federal/appellate-courts/F3/203/596/474793/) Â· [Google v. Oracle (CRS)](https://www.congress.gov/crs-product/LSB10597) Â· [Google v. Oracle (Harvard Law Review)](https://harvardlawreview.org/print/vol-135/google-llc-v-oracle-america-inc/) Â· [SAS v. World Programming](https://en.wikipedia.org/wiki/SAS_Institute_Inc_v_World_Programming_Ltd) Â· [EU Directive 2009/24/EC](https://eur-lex.europa.eu/legal-content/EN/TXT/PDF/?uri=CELEX:32009L0024) Â· [EFF Reverse Engineering FAQ (DMCA Â§1201(f))](https://www.eff.org/issues/coders/reverse-engineering-faq)

Project precedents: [ReactOS suspends development for source code review](https://www.linux.com/news/reactos-suspends-development-source-code-review/) Â· [ReactOS Code Audit (Slashdot)](https://slashdot.org/story/06/02/01/1944257/reactos-code-audit) Â· [ReactOS / Windows Research Kernel claim (The Register, 2019)](https://www.theregister.com/2019/07/03/reactos_windows_research_kernel_claim/) Â· [Wine legal issues discussion](https://forum.winehq.org/viewtopic.php?t=7138&start=25) Â· [Wine RE for development](https://forum.winehq.org/viewtopic.php?t=41541) Â· [GNUstep Developer FAQ](https://mediawiki.gnustep.org/index.php/Developer_FAQ) Â· [GNUstep Contributing](http://developer.gnustep.org/About/Contributing/index.html)

Licensing: [FSF opinion on the APSL](https://www.gnu.org/philosophy/apsl.en.html) Â· [Apple Public Source License](https://en.wikipedia.org/wiki/Apple_Public_Source_License) Â· [GNU LGPL](https://en.wikipedia.org/wiki/GNU_Lesser_General_Public_License) Â· [Swift license + Runtime Library Exception](https://www.swift.org/legal/license.html) Â· [Swift LICENSE.txt](https://www.swift.org/LICENSE.txt) Â· [Cocotron](https://www.cocotron.org/) Â· [Cocotron overview](https://cocoadev.github.io/CocoTron/) Â· [Darling](https://github.com/darlinghq/darling) Â· [Darling: macOS compatibility for Linux (LWN)](https://lwn.net/Articles/794871/) Â· [Cocotron-as-Cocoa discussion](https://github.com/darlinghq/darling/issues/39) Â· [darling-cocotron NSControl.m](https://github.com/darlinghq/darling-cocotron/blob/master/AppKit/NSControl.m) Â· [Cairo Graphics](https://www.cairographics.org/) Â· [Skia Graphics Engine](https://en.wikipedia.org/wiki/Skia_Graphics_Engine) Â· [Licensing WebKit](https://webkit.org/licensing-webkit/)

Apple terms and trademarks: [Xcode and Apple SDKs Agreement](https://www.apple.com/legal/sla/docs/xcode.pdf) Â· [Apple Trademark List](https://www.apple.com/legal/intellectual-property/trademark/appletmlist.html) Â· [Apple Guidelines for Third Parties](https://www.apple.com/legal/intellectual-property/guidelinesfor3rdparties.html) Â· [Apple Developer Program License Agreement](https://developer.apple.com/support/terms/apple-developer-program-license-agreement/)

Naming precedent and collision: [Microsoft Corp. v. Lindows.com](https://en.wikipedia.org/wiki/Microsoft_Corp._v._Lindows.com,_Inc.) Â· [Microsoft to pay $20M to end Lindows battle](https://www.computerworld.com/article/1464816/microsoft-to-pay-20m-to-end-lindows-trademark-battle.html) Â· [openosx.com](https://openosx.com/) Â· [OpenOSX Inc. company profile](https://www.zoominfo.com/c/openosx-inc/147292688) Â· [LibreCorp/OpenOSX](https://github.com/LibreCorp/OpenOSX)

*Note: this is engineering-informed analysis of public sources, not legal advice. Items 1 and 8 in the checklist are the two decisions worth a paid hour with an IP attorney before you commit to them publicly.*
