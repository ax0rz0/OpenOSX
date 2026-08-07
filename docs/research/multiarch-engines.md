# Task B â€” Cross-Architecture Binary Translation Engines for OpenOSX

**Scope:** reusable open-source DBT engines for (1) arm64 guest â†’ x86_64 host (primary: "reverse Rosetta"), (2) x86_64 guest â†’ arm64 host (secondary). Includes hazard analysis, license review, and a decisive build recommendation.

**Clean-room note:** everything below is sourced from public documentation, published reverse-engineering write-ups, peer-reviewed papers, and permissively licensed open source. No recommendation involves obtaining leaked Apple source, or extracting/redistributing `/usr/libexec/rosetta/*` or `/var/db/oah/*`.

---

## 0. Executive summary

| | Direction 1 (primary) | Direction 2 (secondary) |
|---|---|---|
| **Guest â†’ Host** | arm64 â†’ x86_64 | x86_64 â†’ arm64 |
| **Recommended core** | **dynarmic** (A64 frontend + x86-64 backend), 0BSD | **FEXCore** (from FEX-Emu), MIT |
| **Prior art density** | Thin â€” but one production-grade engine exists | Thick â€” three mature engines |
| **Memory ordering** | **Free.** Weak-on-strong is sound by construction | **Expensive.** Needs fences everywhere or TSO hardware |
| **Register pressure** | **Hard.** 31+32 guest regs â†’ 16+16 host regs | Easy. 16+16 guest regs â†’ 31+32 host regs |
| **Realistic perf** | 25â€“50% of native | 35â€“55% of native (ARMv8.2+); ~71% only with Apple's HW |
| **Priority** | Build first | Build later, maybe never |

**The single biggest finding is a structural asymmetry that runs in your favour on the direction you care about, and against you on the one you don't:**

- **Memory ordering**: arm64-on-x86 is the *trivially sound* direction. Every published paper in this space (Risotto, Lasagne, Arancini) exists to solve **strong-on-weak** (x86 â†’ Arm). Nobody writes papers about weak-on-strong because there is nothing to solve. You get for free the thing Apple had to add custom silicon for.
- **Register allocation**: arm64-on-x86 is the *hard* direction, and dougallj's Rosetta 2 analysis calls this out explicitly â€” ARM's 32 registers absorb x86's 16 without spilling, but the reverse "would necessitate constant memory operations, making this architecture less viable." This is where your performance goes, not memory ordering.

**The second biggest finding is strategic:** the first and best customer of the arm64â†’x86_64 translator is **OpenOSX itself**. Because this tree builds a complete userland for both architectures from one source commit, a working translator lets you run the *arm64 userland's own test suite* on x86_64 GitHub Actions runners. That closes the arm64 bring-up loop (Task A) with zero ARM hardware and zero flakey QEMU-system boots. That alone justifies building it, independent of any app-compatibility payoff.

**The honest caveat:** binary translation solves the easy half of macOS app compatibility. A translated arm64 `/bin/ls` will run long before a translated Xcode does, and the gap is entirely the missing closed-source frameworks (AppKit, Metal, CoreGraphics), not the ISA. See Â§7.

---

## 1. The architectural advantage, stated precisely

Your framing is correct and worth making sharper, because it changes which engine to pick.

Wine must emulate an *OS*: hundreds of Win32 APIs, a registry, a different threading model, a different object model. box64 must hand-wrap hundreds of host libraries with per-function ABI thunks. QEMU-user must translate every Linux syscall into a host Linux syscall, coping with per-architecture syscall numbering, per-architecture `struct stat`, and so on.

OpenOSX has to do **none of that**, for three compounding reasons:

1. **Same OS.** Guest and host are Darwin. The system-call surface is not merely similar, it is *identical*.
2. **Same syscall numbers.** Darwin generates syscall numbers from a single `syscalls.master` shared by all architectures. `SYS_write` is 4 on x86_64 and 4 on arm64. Mach traps likewise share their negative-number table. This is *not* true on Linux (where every architecture has its own numbering) and is the reason qemu-linux-user's syscall layer is 20k lines. Ours is a register shuffle plus a class-bit fixup.
3. **Same source tree, both slices.** You can ship a genuine arm64 `dyld`, `libSystem`, `libobjc`, `CoreFoundation`, `Foundation`, `libxpc`, `libc++` built from the same commit as the host userland. This is exactly the Rosetta 2 model (x86_64 system dylib slices resident on Apple Silicon) â€” except Apple had to maintain two toolchain configurations for a decade to get there, and you get it as a side effect of `flake.nix` already having `image-arm64-*` targets.

The consequence for engine selection: **you do not need an engine with good library-thunking infrastructure.** box64's headline performance advantage comes almost entirely from *replacing* guest libc/SDL/GL with native host libraries. Strip that away â€” as you must, because you have real guest dylibs and want real guest semantics â€” and box64's advantage over FEX largely evaporates. Pick engines on the quality of their **ISA core**, not their thunk tooling.

### 1.1 The single boundary

With the guest running an entirely guest-architecture userland, the translator intercepts exactly one instruction class plus a short list of special addresses:

| Boundary | arm64 guest on x86_64 host |
|---|---|
| BSD syscall | `svc #0x80`, number in `x16`, args `x0â€“x5`, error in carry flag â†’ host `syscall`, number in `rax` (`\|0x2000000`), args `rdi,rsi,rdx,r10,r8,r9`, error in CF |
| Mach trap | same `svc`, negative number in `x16` â†’ host `syscall` with `\|0x1000000` |
| Thread self | `mrs xN, TPIDRRO_EL0` (mask low 3 bits = CPU id) â†’ host `mov %gs:0x0, %rN` |
| Timebase | `mrs xN, CNTVCT_EL0` â†’ scaled host TSC / `mach_absolute_time` |
| commpage | guest reads `_COMM_PAGE64_BASE_ADDRESS` (arm64 layout, different address from x86_64's) â†’ synthetic page mapped at the guest-expected VA, populated from host values |
| Cache maint | `IC IVAU`, `DC CVAU`, `ISB` â†’ translation-cache invalidation hints |

That is the whole marshalling layer. Everything else â€” struct layouts, errno values, `mach_port_t` semantics, dispatch queues, XPC â€” is bit-identical because both sides are LP64 Darwin compiled from the same headers.

One genuinely fiddly item survives: **signals**. The host kernel delivers a host-arch `ucontext_t`/`mcontext_t`; the translator must synthesise the guest-arch equivalent on the guest stack, resume translated execution at the guest handler, and unwind correctly on `sigreturn`. Budget real time for this; it is the classic DBT tarpit and it is where FEX and box64 both accumulated the most bug reports.

---

## 2. Direction 1 â€” arm64 â†’ x86_64 (primary)

### 2.1 Candidate engines

#### dynarmic â€” **recommended**
- **License: 0BSD** (public-domain-equivalent, no attribution required). Dependencies are all permissive: xbyak (BSD-3), zydis (MIT), fmt (MIT), robin-map (MIT), mcl, oaknut, biscuit.
- **Frontends:** ARM v3 through v7-A, 32-bit v8, **64-bit v8 (A64)**.
- **Backends:** **x86-64** and AArch64. No 32-bit host support, ever.
- **Maturity: production.** This is the engine Nintendo Switch emulators (yuzu and its forks) use to run AArch64 game code on x86-64 desktops. It is battle-tested against some of the most demanding AArch64 workloads that exist outside Apple's ecosystem â€” real games, real JITs, real multithreading. The A64 frontend was written specifically at yuzu's request because dynarmic outperformed Unicorn.
- **Design:** decode â†’ custom SSA-ish IR â†’ optimisation passes â†’ register allocation â†’ xbyak-emitted host code. Splits emitted code into "near"/"far" regions to keep hot paths in L1I.
- **Linux ties: essentially none.** This is the decisive advantage. dynarmic is a *library*, not a program. It has no ELF loader, no syscall layer, no `/proc` dependency, no rootfs model. It emulates user-mode CPU only and hands you callbacks for memory access and for `SVC`. It expects to be embedded in a host application that provides the OS. **That host application is what we write.**
- **Documented deviations (from its own README):** user-mode only; FPSR state is approximate; misaligned-access trapping is incomplete; exclusive-monitor (`LDXR`/`STXR`) behaviour may not match real silicon; no formal verification or security audit.

#### QEMU user-mode (`qemu-aarch64`) â€” not recommended
- **License: GPLv2.** Shipping a GPLv2 translator inside an APSL/BSD-flavoured base OS is a policy problem you should not take on voluntarily.
- **Darwin support is gone.** `darwin-user` was orphaned, stopped compiling, and was **deleted from QEMU in 2012** by Andreas FÃ¤rber (reviewed by Peter Maydell) after nobody stepped up to fix it. Only `linux-user` and `bsd-user` remain. You would be resurrecting a 14-year-dead target *and* writing the Mach-O layer.
- **Performance:** TCG is the slowest option by a wide margin â€” in the reverse direction it managed 16% of native on 7-zip against box64's 53%. Expect comparable or worse here.
- **Useful only as a correctness oracle**, run out-of-tree during development.

#### Academic / research
- **Lasagne** (PLDI 2022, St Andrews/TU Delft/TUM) â€” end-to-end **static** binary translator with formally proved memory-model mappings *between x86 and Arm in both directions*, plus fence-reduction optimisations (45.5% average fence reduction, up to ~65%). Not shippable, but it is the authoritative reference for the ordering-mapping rules in Â§2.3. Read it before writing a single barrier.
- **mc2llvm** â€” LLVM-based retargetable translator with explicit AArch64â†’x86-64 support, including an ICPP'19 companion paper specifically on **translating the AArch64 floating-point instruction set to x86-64**. That paper is the single most directly relevant published artefact for Â§2.5. Reported 47% of native (static, EEMBC) / 24.5% (dynamic, SPEC CINT2006) â€” a useful sanity bound on what this direction achieves.
- **LLBT** â€” ARMâ†’LLVM IRâ†’{x86, x86-64, ARM, MIPS}; ~6Ã— faster than QEMU, 2.3Ã— faster than HQEMU.
- **Risotto** (ASPLOS'23) and **Arancini** (ASPLOS'26) â€” both strong-on-weak, so not our direction, but Risotto contributes a directly relevant idea: **cross-architecture dynamic linking of native shared libraries**, i.e. letting emulated code call host-native libraries across the ABI boundary. That is the mechanism behind the "native fast-path override" design in Â§2.7.

#### The honest assessment
This direction is genuinely under-served relative to x86â†’arm. There is no FEX-equivalent, no box64-equivalent, no vendor-backed product, and no Darwin-aware anything. But the claim sometimes made â€” "reverse Rosetta doesn't exist, therefore it's infeasible" â€” conflates *nobody has commercial reason to build it* with *it can't be built*. dynarmic is an existence proof that the ISA half is solved and permissively licensed. What's missing is the Darwin half, which you have to write no matter which engine you pick.

### 2.2 Register pressure â€” the actual performance problem

This is the dominant cost and it must drive the design.

- Guest AArch64 architectural state: 31 GPRs + SP + PC + NZCV + 32Ã—128-bit vector registers + FPCR/FPSR â‰ˆ 800 bytes.
- Host x86-64: 16 GPRs + 16 XMM (32 ZMM with AVX-512).

You cannot hold guest state in host registers. Mitigations, in order:

1. **Context struct in a pinned host register** (`r15` or `rbp`), guest state accessed as displacement loads/stores. On modern x86 the struct stays hot in L1 and store-to-load forwarding covers back-to-back dependencies (~4â€“5 cycle latency, high throughput). dynarmic already does exactly this.
2. **Per-block register allocation** â€” allocate the 4â€“6 hottest guest registers into host registers for a block/trace, spill at exits. dynarmic's IR + regalloc already does this; the quality of this pass is most of your performance.
3. **Wider hosts.** AVX-512 gives 32 vector registers, enough for a near-1:1 NEON map. Intel **APX** adds r16â€“r31 (32 GPRs), which would allow a near-1:1 GPR map â€” dougallj notes Rosetta's prologue/epilogue tricks were unavailable on x86 "until upcoming Intel APX extensions." **Design the IR/backend boundary so an APX backend can be dropped in later.**
4. **NZCV.** ARM condition flags map to x86 EFLAGS *almost* â€” Nâ†”SF, Zâ†”ZF, Vâ†”OF, but ARM's carry for subtraction is inverted relative to x86's borrow. This is precisely what Apple solved in the other direction with `CFINV` (FEAT_FlagM). Going our way, you invert CF around `SUB`/`SBC`/`CMP` â€” one extra `cmc` or a fold into the consumer. Cheap, but pervasive; make the IR carry a "carry polarity" bit so the optimiser can elide most of the inversions.

**Ballpark: 25â€“50% of native** on integer/FP-heavy code, i.e. 2â€“4Ã— slowdown. Worse than Rosetta 2's 71% precisely because of registers. GPU-bound and I/O-bound code is unaffected.

### 2.3 Memory ordering â€” confirmed: this is the easy direction

**The claim in your brief is correct, and the reasoning is worth writing down because it justifies skipping an entire research literature.**

Order the models by strength: **SC âŠ x86-TSO âŠ ARMv8**. TSO forbids every reordering ARMv8 forbids, plus more (TSO permits only storeâ†’load reordering; ARMv8 permits essentially all four). Therefore **every execution the x86 host can produce is a legal execution of the ARM program.** Soundness is by construction. Plain guest loads and stores translate 1:1 with *zero* fences.

Concrete mapping table:

| arm64 guest | x86_64 host | Cost |
|---|---|---|
| `LDR` / `STR` | `mov` | free |
| `LDAR`, `LDAPR` (acquire) | `mov` | free â€” x86 loads are already acquire |
| `STLR` (release) | `mov` | free for release semanticsâ€¦ |
| `STLR` followed by `LDAR` (SC pair) | `xchg [m], r`, or `mov` + `lock or $0,(%rsp)` | one locked op, rare |
| `DMB ISH` / `DMB SY` | `lock or $0,(%rsp)` (faster than `mfence`) | only where the guest explicitly asked |
| `DSB` | same as `DMB` for user-mode | rare |
| `ISB` | no-op, *but* treat as a translation-cache invalidation hint | â€” |
| **LSE atomics** `LDADD`, `LDCLR`, `SWP`, `CAS`, `CASP` | `lock xadd`, `lock and`, `xchg`, `lock cmpxchg`, `lock cmpxchg16b` | near-perfect 1:1 |
| `LDXR`/`STXR` (LL/SC) | pattern-match the loop â†’ `lock cmpxchg` retry; else emulate an exclusive monitor | the one awkward case |

**LL/SC is the only real wart**, and it is much less of a problem than it looks: `FEAT_LSE` is mandatory from ARMv8.1, every Apple core has it, and clang targeting `apple-m1`/`apple-a14` emits `CAS`/`LDADD` rather than `LDXR`/`STXR`. The macOS arm64 binaries you actually care about are overwhelmingly LSE-based. Keep a monitor-emulation fallback (address + value snapshot; `STXR` becomes `cmpxchg` against the snapshot), accept the classic ABA hazard, and document it â€” dynarmic already ships exactly this deviation.

**Contrast â€” why the other direction is brutal.** Every x86 store is a release-store and every x86 load is an acquire-load. A naive x86â†’ARM translator must emit `STLR`/`LDAR` (or `DMB`) on *essentially every memory access*. Risotto, Lasagne, and Arancini are three separate research programmes whose entire purpose is minimising those fences with formally verified mapping schemes; Risotto's payoff for all that machinery was 6.7% over "erroneous" QEMU. Apple's answer was to skip software entirely and put a **TSO mode bit in the M1's memory subsystem**, switching a thread's observed ordering model to x86-equivalent â€” measured at only ~8.94% slower than ARM's native weak ordering, versus the multiples that software fencing costs.

**We need none of this.** No fence-placement optimiser, no formal memory-model proof obligation, no hardware crutch. This is the largest single reason the primary direction is more tractable than its scarcity of prior art suggests.

### 2.4 Page size â€” 16KB guest on 4KB host

arm64 Darwin uses **16KB** pages; x86_64 Darwin uses **4KB**. Running a 16KB-page binary on a 4KB-page host is the **benign** direction:

- `LC_SEGMENT_64` `vmaddr`/`vmsize` are 16KB-aligned, and 16384 is an exact multiple of 4096, so `mmap` of guest segments works with no fixup.
- The dangerous case is the reverse â€” 4KB-granular x86 binaries on a 16KB host â€” where a single 16KB host page can span segments with different protections. That's why Apple Silicon supports a 4K granule and why Rosetta processes need sub-page mapping care.

Real issues in our direction, and the fixes:

1. **`vm_page_size` / `getpagesize()` will report 4096** to a guest expecting 16384. For *our own* arm64 sysroot this is a non-problem: **build the translation sysroot with `PAGE_MAX_SHIFT=12`.** Darwin deliberately exposes `vm_page_size` as a runtime variable (rather than a compile-time `PAGE_SIZE`) precisely for this, and you control the build. Apple never had this luxury.
2. **Third-party macOS arm64 apps** may bundle 16KB-baked allocators â€” jemalloc built `--with-lg-page=14` is the canonical offender (Chromium and derivatives, Rust, Geekbench all ship it), as are GC heaps in SpiderMonkey/V8. Mitigation: have the translator's `mmap`/`mach_vm_allocate` shim **round guest allocations up to 16KB and align them to 16KB**. Costs address space (irrelevant at 64-bit) and some RSS; buys you correct behaviour for any guest that assumes 16KB granularity for `mprotect`/`mach_vm_protect`. Do this from day one; it is ~30 lines and retrofitting it after chasing a jemalloc crash is miserable.
3. **Do not** attempt to build the x86_64 XNU with 16KB pages. The x86_64 pmap is structurally 4KB (`I386_PGBYTES`); this is not a tunable.

### 2.5 NEON â†’ SSE/AVX

Broadly tractable â€” both are 128-bit SIMD â€” with a well-defined set of awkward cases. Require SSE4.2 + AES-NI + FMA3 as the host baseline (Haswell/2013+); use AVX2 opportunistically.

**Clean 1:1 or near:** integer add/sub/mul, logicals, shifts, compares, `UMIN`/`SMAX` etc. (SSE4.1 covers 32-bit widths), FP add/mul/div/sqrt, `FMLA`/`FMLS` â†’ `vfmadd*`/`vfmsub*`, `FRINTN`/`FRINTP`/`FRINTM`/`FRINTZ` â†’ `roundsd`/`roundps` with the right `imm8`, `PMULL` (64Ã—64) â†’ `pclmulqdq`.

**Awkward, needs multi-instruction sequences:**

| NEON | Problem | Approach |
|---|---|---|
| `TBL` / `TBX` | `pshufb` zeroes a lane when the index's *high bit* is set; `TBL` zeroes when the index is *out of range* | compare-against-limit + mask, then `pshufb` |
| `ADDP`, `FADDP` (pairwise) | no direct SSE analogue for all widths | `phaddd`/`haddps` where available, else shuffle+add |
| `ADDV`, `FMAXV`, `UMAXV` (across-lane reduction) | none | logâ‚‚(n) shuffle+op tree |
| `UMULL`/`SMULL`, `SADDL`, `SQXTN` (widening/narrowing) | partial SSE coverage | `pmuludq`/`punpck*` sequences; `packsswb`/`packuswb` for the widths that exist, manual saturation otherwise |
| saturating arith (`SQADD`, `UQSUB`) | SSE has `padds*`/`psubs*` only for 8/16-bit | manual compare+select for 32/64-bit |
| `CNT` (per-byte popcount) | none | nibble-LUT + `pshufb` (classic MuÅ‚a trick) |
| `RBIT`, `CLS` | none | bit-reversal via `pshufb` LUT; `CLS` via `xor`+`lzcnt` |
| `AESE`/`AESD`/`AESMC` | ARM splits the round differently from x86: `AESE` = SubBytes+ShiftRows+AddRoundKey (no MixColumns); `AESENC` = all four | well-documented public fixup: `aesenc` with a zero round key, plus separate `aesenclast`/`aesimc` composition |
| `SHA1*`/`SHA256*` | x86 SHA-NI exists but with different operand shapes | map where possible, scalar fallback when the host lacks SHA-NI |

**Floating-point semantics are the sharp edge, not the opcodes.** ARM's `FPCR` (DN, FZ, FZ16, RMode, AHP) and x86's `MXCSR` (FTZ, DAZ, RC) do not correspond. Specifically:

- **NaN propagation:** ARM with `FPCR.DN=1` produces a *default* NaN; x86 propagates the first source QNaN. Different bit patterns out.
- **min/max on NaN:** x86 `minps`/`maxps` return the **second** operand when either input is NaN; ARM `FMIN`/`FMAX` return the NaN, and `FMINNM`/`FMAXNM` return the *number*. Three distinct behaviours to reconcile.
- **Tininess detection** (before vs after rounding) differs, affecting underflow flag semantics.

Each needs a 2â€“4 instruction fixup. Do what FEX does and expose a **config knob** ("accurate FP" vs "fast FP") so users can trade exactness for speed per-app. The ICPP'19 paper *Translating AArch64 Floating-Point Instruction Set to the x86-64 Platform* is literally about this problem in this direction â€” treat it as the spec.

**SVE / SVE2 / SME:** Apple Silicon does not implement SVE for general code (M4 added SME2). Detect and refuse; revisit only if macOS apps start shipping SME kernels.

### 2.6 Pointer authentication (arm64e)

**Recommendation: declare arm64e out of scope. Target plain `arm64` only.**

Justification: third-party macOS apps build as `arm64` by default and run with PAC keys disabled; only Apple's own binaries ship `arm64e`. A plain `arm64` binary contains no PAC instructions at all. And because we supply our own arm64 system dylibs built from OpenOSX sources, we simply build them without `-mbranch-protection` â€” the exact problem Apple couldn't dodge, we sidestep by construction.

If arm64e ever becomes necessary: PAC *instructions* are easy to emulate (a keyed 64-bit tweak â€” SipHash-style, functionally correct, explicitly not a security boundary; `PACIASP`/`AUTIASP` sit in the HINT space and are architecturally NOPs on non-PAC hardware anyway). The *hard* part is the arm64e **ABI**: signed vtable entries, signed ObjC `isa`, signed block `invoke` pointers, each with its own discriminator. That is a large, fiddly project. Don't.

### 2.7 objc_msgSend, ARC, and indirect branches â€” the real hot path

For Objective-C and Swift workloads, arithmetic translation is not the bottleneck. **Indirect branches are.**

Every translated indirect branch (`br xN`, `blr xN`, `ret`) requires a guest-PC â†’ host-code-pointer lookup. `objc_msgSend`'s arm64 fast path is roughly ten instructions ending in a `br x17` into the cached IMP â€” so *every Objective-C message send is an indirect branch*, and an ObjC-heavy UI does millions per second. Translated naively at 2â€“4 host instructions per guest instruction plus a hash lookup, expect ObjC dispatch to run ~2â€“3Ã— slower than native, and it is on the critical path of everything.

Mitigations, in increasing order of leverage:

1. **Inline indirect-branch-target cache** â€” emit a 2-way inline check (compare guest target against two cached values, jump directly on hit) before falling back to the hash table. Standard DBT technique, big win.
2. **Return-address stack** â€” translate guest `BL`/`RET` into host `call`/`ret` with side bookkeeping so the host CPU's return-address predictor stays accurate. This is exactly what Rosetta 2 does in the other direction (rewriting x86 `CALL`/`RET` into ARM `BL`/`RET` with stack bookkeeping), and it is worth a lot.
3. **Native fast-path overrides â€” the highest-leverage idea in this report.** Maintain a table of `symbol name â†’ hand-written host-native implementation`, applied by the loader when the guest `dyld` binds. Because **we own libobjc's and libdispatch's source**, the guest data structures are documented-by-construction rather than reverse-engineered. Candidates:
   - `objc_msgSend` / `objc_msgSendSuper2` â€” walk the guest ObjC method cache in native x86_64, then jump straight to the *translated* IMP without a full guest round-trip.
   - `objc_retain` / `objc_release` / `objc_autorelease` and `swift_retain` / `swift_release` â€” ~15 instructions each, called constantly under ARC; native versions manipulating the guest side-tables are a large, broad win.
   - `memcpy`, `memmove`, `memset`, `bzero`, `strlen`, `_platform_memmove` â€” dispatch to host AVX2 implementations.
   - `os_unfair_lock_lock/unlock`, `pthread_mutex_lock/unlock`, `dispatch_once`.
   
   This is Risotto's "cross-architecture dynamic linking of native shared libraries" and box64's library-wrapping idea, applied **surgically to ~30 hot leaf functions** rather than to whole libraries. Keeping it to leaf functions with trivial signatures avoids the struct-ABI-marshalling swamp that makes box64's whole-library approach so labour-intensive, while capturing most of the benefit.
4. **Memoise IMP translations in the ObjC cache line** â€” since we control the cache layout, co-locate the host code pointer with the guest IMP.

### 2.8 Guest-side JIT (W^X)

Guest apps with their own JITs â€” JavaScriptCore, V8, .NET, Java â€” generate guest code at runtime. Requirements: track guest RWX regions, invalidate the translation cache on write, honour `pthread_jit_write_protect_np`.

**This is markedly easier with an arm64 guest.** AArch64 requires *explicit* instruction-cache maintenance (`IC IVAU` + `DSB` + `ISB`) after writing code, so a correct arm64 guest **tells you** when it has modified code. x86 guests need no such notification, forcing write-protection-based detection with all its cost and corner cases. Another asymmetry favouring the primary direction. Keep a conservative write-barrier fallback for guests that cheat.

---

## 3. Direction 2 â€” x86_64 â†’ arm64 (secondary)

### 3.1 Candidates

| Engine | License | Design | 7-zip vs native (2022) | Linux coupling |
|---|---|---|---|---|
| **FEX-Emu** | **MIT** | Custom IR + opt passes + ARM64 JIT; full SSEâ†’AVX2; experimental code cache; per-app config incl. "skip costly memory model emulation"; thunking for GL/Vulkan | 26% (much improved since; AVX and regalloc work landed 2024â€“25) | Heavy but *cleanly separated*: `FEXCore` is arch-agnostic; ELF loader, Linux syscalls, rootfs, binfmt_misc all live in separate `LinuxEmulation`/`HLE` layers |
| **box64** | **MIT** | Splatter dynarec, less IR sophistication; 5â€“10Ã— over its interpreter | 53% | Heavy **and** structural: performance depends on wrapping native host libs (libc, SDL, GL) with hand-written thunks |
| **QEMU TCG** | GPLv2 | Portable IR, slowest | 16% | linux-user / bsd-user only; darwin-user deleted 2012 |
| **Rosetta 2** | Proprietary | AOT + JIT hybrid | **71%** | n/a â€” study only |

*(Numbers from the box86.org March 2022 cross-comparison; FEX has improved substantially since and box64's figure includes native-library wrapping. Treat as ordering, not gospel.)*

### 3.2 What is legally learnable from Rosetta 2

Its **architecture** is well documented publicly (Apple's platform security guide, dougallj's analysis, FFRI's Project Champollion RE series). Copy the *design*, never the bits:

- **AOT the entire `__TEXT` at first launch**, cache the result, JIT only for runtime-generated code. Champollion documents `oahd` intercepting exec, spawning `oahd-helper`, and caching under `/var/db/oah/` keyed by **SHA-256 of binary contents + execution path**, written to `.aot.in_progress` then atomically renamed. Excellent design; steal it.
- **The AOT output is a normal arm64 Mach-O** referencing the original x86_64 binary for constant data, with a custom `LC_AOT_METADATA` load command and a fixed guestâ†”host register map.
- **A runtime** mapped into every translated process that owns address resolution, lazy binding (cached in a `__stubs_sh`-style section), and JIT.
- **Red-black trees** correlating guest â†” host addresses.
- **An AOT shared cache**, playing the role dyld's shared cache plays natively.
- **One-to-one instruction mapping preserves code locality** â€” better for I-cache and branch prediction than execution-order translation.

**Hardware crutches we will not have on generic ARM:** the TSO mode bit; `FEAT_AFP` (x86-compatible FP semantics in hardware); `FEAT_FlagM`/`FlagM2` (`CFINV`, `RMIF`, `SETF8/16`, `AXFLAG`/`XAFLAG`); and an undocumented M1 extension computing x86 parity and adjust flags in hardware. On stock ARMv8.0 silicon, budget 25â€“50% of native, not 71%. Note also that Rosetta does **full 80-bit x87 software emulation** (slower but more correct than Windows-on-ARM's 64-bit approximation) â€” decide your own position on that trade.

**Legal line:** reading Champollion and dougallj is fine â€” published research on observable behaviour. Extracting, shipping, or deriving code from `/usr/libexec/rosetta/*` or AOT cache artefacts is not. Keep a written record that no Apple binary was disassembled *into* the implementation.

### 3.3 Priority argument

This direction matters far less than it appears. If OpenOSX runs on Apple Silicon or ARM hardware, **modern Mac apps there are already arm64 â€” native, no translation.** The x86_64â†’arm64 path only serves *legacy Intel-only* Mac apps, a shrinking set, on a host configuration you don't yet have. Meanwhile the arm64â†’x86_64 path serves the *growing* set (arm64-only apps) on the host configuration you actually ship today. Build direction 1 first, and don't feel bad about direction 2 slipping a year.

---

## 4. Recommendation

**One project, one IR boundary, two frontends and two backends. Call it `oxtrans`.**

### Direction 1: build on dynarmic. Decisively.

Take dynarmic's **A64 frontend + x86-64 backend** as the execution core and write the Darwin layer yourself.

**Why dynarmic and not the alternatives:**
- 0BSD is the most permissive license in the field â€” zero friction in an APSL/BSD image, no attribution burden, no GPL contamination question.
- It is the *only* production-grade, permissively licensed arm64â†’x86-64 JIT in existence. Everything else in this direction is a research prototype or GPL.
- Its Linux coupling is **zero**, because it is a library that emulates a CPU and nothing else. Every other candidate would require ripping out an OS personality layer first. dynarmic has no OS personality to rip out â€” which is exactly the shape you need, since the OS layer is the part you have a unique advantage in writing.
- It is proven against hostile workloads (game code, guest JITs, heavy multithreading), not toy benchmarks.

**What you write on top:**
1. Foreign-arch Mach-O loader stub (~500 lines) â€” map guest `dyld`, synthesise the kernel-style stack and `apple[]` array, jump to its entry. **Do not reimplement Mach-O loading**: let the guest `dyld` do all binding, and you get two-level namespaces, weak binding, chained fixups, `__DATA_CONST`, ObjC image registration, and the shared cache *for free*, all executing under translation.
2. `SVC` / Mach-trap bridge (Â§1.1) â€” a register shuffle, not a syscall emulator.
3. Signal / `mcontext` translation. The tarpit; budget accordingly.
4. commpage synthesis, `TPIDRRO_EL0` â†’ `%gs` mapping, `CNTVCT_EL0` â†’ timebase.
5. 16KB allocation-granularity shim (Â§2.4).
6. Native fast-path override table (Â§2.7).
7. AOT cache in the Rosetta shape (Â§3.2).

**Do NOT** use dynarmic's `UserCallbacks` memory interface for guest loads/stores. Guest and host share one address space â€” identity-map the guest and emit direct host memory accesses. Verify early that the address ranges don't collide: **both** arm64 and x86_64 Darwin main executables link at `0x100000000`, so the `oxtrans` host binary must be linked at an unusual base (e.g. `-image_base 0x200000000`) or be a dylib behind a tiny stub, and must reserve the guest's canonical range before loading. The dyld shared cache regions differ between the two arches, so those don't collide. **Check this on day one of T1** â€” it is cheap to design around and expensive to retrofit.

**The main risk to this recommendation, stated honestly:** dynarmic's A64 frontend was built for the Nintendo Switch's Cortex-A57 â€” broadly ARMv8.0-A. Apple-targeted code (`-mcpu=apple-m1`) is ARMv8.5-ish and uses **LSE atomics, FP16, dotprod, and other v8.1â€“v8.5 additions** that dynarmic may decode incompletely. Expect to **extend the A64 decoder meaningfully.** Mitigation: the work is mechanical and additive (the decoder is table-driven), it's exactly the work you'd do from scratch anyway, and it's a fraction of the ~1â€“2 person-years a from-scratch AArch64 JIT of comparable quality would cost. Use **Capstone** (BSD-3) as a cross-check decoder and ARM's machine-readable ISA specification to drive exhaustive decoder coverage tests (review ARM's spec license terms before vendoring anything from it).

**Escape hatch:** if dynarmic's A64 coverage or FP fidelity proves inadequate, an **LLVM ORC-based AOT path** (Apache-2.0 with LLVM exception) modelled directly on Rosetta's AOT design gives a much higher performance ceiling â€” with JIT retained only for guest-generated code. Far more work. **Design the IR boundary now so this can be swapped in later without touching the Darwin layer.**

### Direction 2: build on FEXCore. Not box64, not QEMU.

MIT; the best IR and optimiser in the field; full AVX2; actively developed. Port plan: keep `FEXCore` (IR, opt passes, ARM64 JIT), replace `LinuxEmulation` with a Darwin equivalent, and **reuse the same Mach-O loader stub, syscall bridge, and signal bridge written for direction 1**.

Reject box64 despite its better 2022 benchmarks: that advantage comes from native-library wrapping, which is precisely the technique we don't want (we have real guest dylibs) â€” and without it you're left with a less sophisticated splatter JIT. Reject QEMU on license and speed.

If OpenOSX ever runs on Apple Silicon and can flip the TSO bit from EL1 (an implementation-defined system register, publicly documented by the Asahi Linux project), you get Rosetta-class memory ordering for free and this direction becomes genuinely fast.

### Share everything above the ISA core

Loader, syscall bridge, signal bridge, commpage, fast-path table, AOT cache format, `sysctl.proc_translated` plumbing, kernel imgact hook. **That shared layer is ~70% of the total work and it is exactly where the Darwin-on-Darwin advantage pays off.** Structure the repo so the ISA cores are pluggable backends behind it from commit one.

### Purpose-built translator? No.

Write the Darwin layer (unavoidable, and it's where your unique advantage lies). Reuse the ISA cores (a solved, expensive, permissively licensed problem where you have no advantage at all).

---

## 5. License summary for anything shippable

| Component | License | Ship in an APSL/BSD image? |
|---|---|---|
| dynarmic | 0BSD | âœ… ideal |
| dynarmic deps (xbyak BSD-3, zydis MIT, fmt MIT, robin-map MIT, mcl, oaknut, biscuit) | permissive | âœ… |
| FEX-Emu / FEXCore | MIT | âœ… |
| box64 / box86 | MIT | âœ… (but not recommended technically) |
| Capstone | BSD-3 | âœ… |
| LLVM (ORC, if AOT path) | Apache-2.0 + LLVM exception | âœ… |
| SIMDe (NEONâ†”SSE semantic reference) | MIT | âœ… |
| Intel `ARM_NEON_2_x86_SSE` | BSD-2 | âœ… reference for Â§2.5 |
| QEMU / TCG | **GPLv2** | âš ï¸ dev-time oracle only, out of tree |
| Unicorn Engine | **GPLv2** | âš ï¸ same |
| Darling | **GPL** | âš ï¸ read for ideas, don't vendor |
| Rosetta 2 | Proprietary | âŒ never â€” architecture only |

---

## 6. Concrete milestone sequence

| # | Milestone | Proves |
|---|---|---|
| **T0** | Build `image-arm64` and `image-x86_64` from the same commit; install the arm64 sysroot into the x86_64 image at `/System/OpenOSX/Translation/aarch64/` with its own shared cache | The dual-slice foundation. Verify with `otool -h`. |
| **T1** | `oxtrans-run <static arm64 binary>` â€” no dyld, no libSystem, just `_start` â†’ `write(2)` â†’ `exit` | dynarmic core + `SVC` bridge + segment mapping + **address-space collision check** |
| **T2** | Guest `dyld` bootstrap; run a dynamically-linked arm64 `/bin/echo` against the arm64 sysroot | The whole loader story |
| **T3** | pthreads, signals, `mach_absolute_time`, commpage. **Run the arm64 userland's own test suite under translation and diff against native results** | Correctness â€” and this is the milestone that pays for the whole project (see Â§7) |
| **T4** | Kernel imgact hook: cputype mismatch â†’ transparent re-exec via `oxtrans`; `P_TRANSLATED` flag; `sysctl.proc_translated` | Transparency â€” `./arm64binary` just works |
| **T5** | AOT cache (translate `__TEXT` at first run, key on cdhash + path, atomic rename) | Startup latency |
| **T6** | Native fast-path overrides for the ~30 hot symbols; benchmark | Performance |
| **T7** | Point it at a real macOS arm64 app bundle | Merges with Task #12 |

**Differential testing â€” use the Mac (Task #14).** Run identical instruction sequences natively on real Apple Silicon and under `oxtrans`, and diff register/flag/memory state. This is the highest-value use of that build node and it makes the FP-semantics and flag-polarity work (Â§2.2, Â§2.5) tractable rather than a guessing game.

**Code signing:** XNU verifies cdhashes on page-in, and a translated process executes host-arch code not covered by the guest binary's signature. Rosetta solves this by having the kernel verify the *original* x86_64 pages as they fault in while the AOT output carries a system signature. For OpenOSX (AMFI-relaxed, SIP off) the answer is simply: don't enforce. Note it and move on.

---

## 7. The strategic honesty section

Two things worth stating plainly before this gets scheduled.

**1. The first customer is OpenOSX, not app compatibility.** T3 â€” running the arm64 userland's own test suite under translation on x86_64 CI â€” is worth more to this project in the next year than running any Mac app. It closes the arm64 bring-up loop on free GitHub Actions x86 runners, with no ARM hardware, no QEMU-system boot flakiness, and full debugger access. No other project in this space has that option, because no other project builds both slices from one tree. **Sequence the work so T3 lands early and gets used, and everything after it is upside.**

**2. Translation solves the easy half of app compatibility.** The wall you hit at T7 will not be the ISA â€” dynarmic will chew through arm64 code fine. It will be **AppKit, Metal, CoreGraphics, CoreAnimation, and the rest of the closed-source framework stack that OpenOSX does not have.** A translated arm64 `/bin/ls` will work in month three; a translated arm64 GUI app requires the entirety of Task #12 first. Position this internally as *"future-proofing plus a testing superpower"*, not as *"OpenOSX runs Mac apps"* â€” otherwise the T7 wall reads as failure when it is actually the expected handoff point to a much larger, separate project.

---

## Sources

- [FEX-Emu/FEX (GitHub)](https://github.com/FEX-Emu/FEX) Â· [FEX LICENSE (MIT)](https://github.com/FEX-Emu/FEX/blob/main/LICENSE) Â· [fex-emu.com](https://fex-emu.com/) Â· [FEX ThunkLibs README](https://github.com/FEX-Emu/FEX/blob/main/ThunkLibs/README.md)
- [Box86 / Box64 project site](https://box86.org/) Â· [Box86/Box64 vs QEMU vs FEX vs Rosetta 2 benchmarks](https://box86.org/2022/03/box86-box64-vs-qemu-vs-fex-vs-rosetta2/) Â· [ptitSeb/box64 releases](https://github.com/ptitSeb/box64/releases)
- [dynarmic README (frontends, backends, 0BSD license, limitations)](https://raw.githubusercontent.com/lioncash/dynarmic/master/README.md) Â· [Vita3K/dynarmic](https://github.com/Vita3K/dynarmic/)
- [yuzu Progress Report, April 2022 (dynarmic AArch64â†’AMD64)](https://yuzu-mirror.github.io/entry/yuzu-progress-report-apr-2022/) Â· [yuzu Progress Report, Nov 2023](https://yuzu-emu.org/entry/yuzu-progress-report-nov-2023/) Â· [yuzu NCE on Android](https://hothardware.com/news/yuzu-emulator-nce-boost)
- [QEMU User Mode Emulation docs](https://qemu-project.gitlab.io/qemu/user/index.html) Â· [[PATCH 3/3] Drop darwin-user (qemu-devel, 2012)](https://lists.gnu.org/archive/html/qemu-devel/2012-04/msg04295.html) Â· [Running Arm Binaries on x86 with QEMU-User (Azeria Labs)](https://azeria-labs.com/arm-on-x86-qemu-user/)
- [dougallj â€” Why is Rosetta 2 fast?](https://dougallj.wordpress.com/2022/11/09/why-is-rosetta-2-fast/)
- [FFRI Project Champollion, part 1 â€” AOT files and the Rosetta 2 runtime](https://ffri.github.io/ProjectChampollion/part1/) Â· [part 2 â€” runtime and AOT shared cache](https://ffri.github.io/ProjectChampollion/part2/) Â· [FFRI/ProjectChampollion (GitHub)](https://github.com/FFRI/ProjectChampollion)
- [Rosetta 2 on a Mac with Apple silicon â€” Apple Platform Security](https://support.apple.com/guide/security/rosetta-2-on-a-mac-with-apple-silicon-secebb113be1/web)
- [TOSTING: Investigating Total Store Ordering on ARM (ARCS'23)](https://www.sra.uni-hannover.de/Publications/2023/tosting-arcs23/wrenger_23_arcs.pdf) Â· [Analyzing the memory ordering models of the Apple M1 (JSA)](https://www.sciencedirect.com/science/article/pii/S1383762124000390)
- [Lasagne: A Static Binary Translator for Weak Memory Model Architectures (PLDI'22)](https://dl.acm.org/doi/10.1145/3519939.3523719) Â· [PDF](https://rcor.me/papers/pldi22lasagna.pdf)
- [Risotto: A Dynamic Binary Translator for Weak Memory Model Architectures (ASPLOS'23)](https://dl.acm.org/doi/10.1145/3567955.3567962) Â· [PDF](https://www.st.ewi.tudelft.nl/sschakraborty/papers/risotto-asplos23.pdf)
- [Arancini: A Hybrid Binary Translator for Weak Memory Model Architectures (ASPLOS'26)](https://www.st.ewi.tudelft.nl/sschakraborty/papers/ASPLOS2026-Arancini.pdf)
- [Translating AArch64 Floating-Point Instruction Set to the x86-64 Platform (ICPP'19 workshops)](https://dl.acm.org/doi/10.1145/3339186.3339192)
- [Low Overhead Dynamic Binary Translation on ARM (MAMBO-X64, PLDI'16)](https://pure.manchester.ac.uk/ws/files/56078084/pldi_16.pdf)
- [Arm64EC â€” ABI for mixing x64 and Arm64 (Microsoft Learn)](https://learn.microsoft.com/en-us/windows/arm/arm64ec) Â· [Understanding Arm64EC ABI and assembly code](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi) Â· [ARM64 Boot Camp: ARM64EC and ARM64X explained](http://www.emulators.com/docs/abc_arm64ec_explained.htm)
- [Understanding Memory Page Sizes on Arm64 (Ampere)](https://amperecomputing.com/tuning-guides/understanding-memory-page-sizes-on-arm64) Â· [jemalloc issue #2639 â€” default `--with-lg-page=16` on ARM64](https://github.com/jemalloc/jemalloc/issues/2639) Â· [Mozilla bug 1660006 â€” GC with 16KB pages on Apple arm64](https://bugzilla.mozilla.org/show_bug.cgi?id=1660006)
- [arm64e on macOS (lelegard/arm-cpusysregs)](https://github.com/lelegard/arm-cpusysregs/blob/main/docs/arm64e-on-macos.md) Â· [Apple internals #8: Pointer authentication (Sigreturn Labs)](https://sigreturn.com/blog/pointer-authentication-arm64e/) Â· [The Anatomy of a Mach-O: Structure, Code Signing, and PAC](https://oliviagallucci.com/the-anatomy-of-a-mach-o-structure-code-signing-and-pac/)
- [darlinghq/darling (GPL â€” reference only)](https://github.com/darlinghq/darling) Â· [apple/darwin-xnu mach-o/loader.h](https://github.com/apple/darwin-xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h)

