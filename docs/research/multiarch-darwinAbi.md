# Research Task C â€” Darwin-Specific Translation Plumbing for OpenOSX

**Scope:** how Rosetta 2 is actually wired into Darwin (public/RE'd only), what the equivalent hooks are in *our* XNU 20.5 tree, and a concrete build-out plan. All kernel claims below are grounded in files under `/root/openosx/src/Kernel/xnu` (paths and line numbers given), not from memory.

---

## 0. Executive summary â€” the five findings that shape everything

1. **XNU 20.5 open source still contains the Rosetta scaffolding, with the load-bearing parts redacted.** Apple's internal codename for Rosetta 2 is **Cambria**, and the name survives in our tree. `load_result_t` has an `is_cambria : 1` bit (`bsd/kern/mach_loader.h:81`); `mach_loader.c:1909` consumes it; `kern_exec.c:1503` treats `is_cambria` as equivalent to `dynlinker` for stack setup. **Nothing in the open tree ever sets it.** That single bit is the exact seam OpenOSX slots into.

2. **The kernel-side "translated process" primitives are already present and functional.** `MAP_TRANSLATED_ALLOW_EXECUTE` (`bsd/sys/mman.h:162`, accepted by `kern_mman.c:259`), `vmkf_translated_allow_execute` (`mach_loader.c:1910`), and `proc_is_translated()` (`bsd/sys/proc.h:412`) all exist. `proc_is_translated()` is stubbed to `return 0` (`bsd/kern/kern_proc.c:2212`) â€” it is a one-line implementation away from working, and `ubc_subr.c:4556/4618` already special-cases translated processes for code-signing blob absence.

3. **An emulated foreign-arch commpage is architected for, header shipped, implementation redacted.** `osfmk/arm/cpu_x86_64_capabilities.h` defines a complete x86_64 commpage layout to be synthesised *by an ARM kernel*, at the hardcoded x86_64 address `0x7fffffe00000`, including `x86_64_kIsTranslated 0x4000000000000000` and `x86_64_kVmPageShift` (the 16K-vs-4K page-size lie). `x86_64_sharedpage_rw_addr` is `extern`-declared with no definition anywhere in the tree.

4. **XNU's shared-region machinery is already multi-architecture and the two arches do not overlap in address space.** `vm_shared_region_lookup` keys regions on `sr_cpu_type` (`osfmk/vm/vm_shared_region.c:356`), and `SHARED_REGION_BASE_ARM64 = 0x180000000` vs `SHARED_REGION_BASE_X86_64 = 0x7FFF00000000` (`osfmk/mach/shared_region.h:45,86`). One kernel can host both concurrently. The only blocker is a `#elif !defined(__arm__)` preprocessor guard (`vm_shared_region.c:654`) that compiles the x86_64 case out on ARM kernels â€” a two-line patch.

5. **The user's *primary* direction (arm64 apps on x86_64 OpenOSX) is the technically *easier* one, and this is not intuitive.** x86-64 is TSO; ARM is weakly ordered. Emulating a weak-memory guest on a strong-memory host is free â€” you simply never need barriers. Rosetta 2 only works at speed because Apple put a *hardware* TSO mode in the M1; nobody else has that. Running arm64 guests on stock AMD/Intel needs no such thing. Page size likewise favours this direction: arm64 dylibs are 16K-segment-aligned, which is trivially satisfiable on a 4K host, whereas the reverse needs the `kVmPageShift` hack. **The direction the user cares most about is the one that does not need Apple's silicon.**

---

## 1. How Rosetta 2 is wired into the OS, and the equivalent hook in our kernel

### 1.1 Apple's architecture (published + RE'd)

**Apple's own words** (Platform Security guide) are the authoritative statement on the exec path:

> "the kernel transfers control to a special Rosetta translation stub rather than to the dynamic link editor, `dyld(1)`"

That one sentence establishes the whole model: the hook is **in the kernel's Mach-O loader**, at the point where it would normally load `/usr/lib/dyld`, and the substitution is a *dylinker substitution*, not a re-exec of a wrapper binary.

The rest, from FFRI's Project Champollion reverse-engineering:

| Component | Location / role |
|---|---|
| `oahd` | Per-user daemon. Receives an fd for the x86_64 binary over Mach IPC (via `fileport_makeport`/`fileport_makefd`), SHA-256s **both the file contents and the path it was executed from**, looks for a cached AOT, returns an fd for it. |
| `oahd-helper` | Spawned by `oahd` on cache miss. Does the actual AOT x86_64â†’arm64 compile. Writes `*.aot.in_progress`, then atomically renames to `*.aot`. |
| `runtime` | The "translation stub". Mapped into the target process; **the process entry point is set to `runtime`, not to dyld**. Owns JIT translation for code the AOT pass couldn't see, plus the x86â†’arm address-mapping red-black tree and the `__stubs_sh` lazy-binding cache. |
| AOT files | `/var/db/oah/<hash>/<hash>/<name>.aot`. Plain Mach-O files carrying a private load command `LC_AOT_METADATA` (cmd `0xcacaca01`) that points back at the x86_64 image path and code sections. |
| `aot_shared_cache` | ~2.4 GB. The AOT analogue of `dyld_shared_cache`. Magic `0x6568636143746f41` ("AotCache"). Holds pre-translated arm64 for every dylib in `dyld_shared_cache_x86_64`, plus x86â†”arm address correspondence maps. |
| `/Library/Apple/usr/libexec/oah/` | Install root. "oah" = **O**ld **A**rchitecture **H**ardware, a name inherited from the PowerPC transition. |

The process memory layout Champollion observed is: **x86_64 `__TEXT` (still mapped, for constants and data) â†’ AOT segments â†’ `runtime` segments.** The foreign binary is genuinely loaded by the kernel's normal Mach-O path; only the *entry point* and the *dylinker* are swapped.

Register mapping used by the AOT ABI is a flat, mechanical one: `RAXâ†’x0, RCXâ†’x1, RDXâ†’x2, â€¦ R15â†’x15`, floats in `q0â€“q15`. Emulated x86 state lives permanently in host registers â€” possible only because arm64 has twice the register file.

**The fat-binary format's role** is the enabling trick and it is worth being explicit: Rosetta is not what makes the *system* work. The system dylibs ship as genuine universal binaries with a real x86_64 slice compiled from the same sources. Rosetta translates *third-party* code; the OS underneath a translated process is native x86_64 code that Apple compiled. Rosetta's job at the library boundary is "find the x86_64 slice and translate it too", not "bridge arm64 libraries to an x86 app". **This is precisely the property OpenOSX already has**, and it is the single biggest reason this project is tractable.

### 1.2 The exec path in *our* tree, step by step

The image-activator table (`bsd/kern/kern_exec.c:1738`):

```c
struct execsw {
	int(*const ex_imgact)(struct image_params *);
	const char *ex_name;
}const execsw[] = {
	{ exec_mach_imgact, "Mach-o Binary" },
	{ exec_fat_imgact,  "Fat Binary" },
	{ exec_shell_imgact,"Interpreter Script" },
```

Tried in order at `kern_exec.c:1864`. The relevant flow:

1. **`exec_fat_imgact`** (`kern_exec.c:717`) â€” if magic is `FAT_MAGIC`, calls into `mach_fat.c`, which at line 114 calls `grade_binary(testtype, testsubtype, testfeatures, TRUE)` for each slice and picks the **highest grade**. Grade 0 = unusable.
2. **`exec_mach_imgact`** (`kern_exec.c:989`) â€” records `ip_origcputype`/`ip_origcpusubtype`, honours `posix_spawn` binprefs, then at line 1081:

```c
grade:
	if (!grade_binary(imgp->ip_origcputype, imgp->ip_origcpusubtype & ~CPU_SUBTYPE_MASK,
	    imgp->ip_origcpusubtype & CPU_SUBTYPE_MASK, TRUE)) {
		error = EBADARCH;
		goto bad;
	}
```
3. **`grade_binary`** is per-arch. `bsd/dev/arm/kern_machdep.c:76` handles only `CPU_TYPE_ARM64`/`CPU_TYPE_ARM`. `bsd/dev/i386/kern_machdep.c:55` handles only `CPU_TYPE_X86_64`/`CPU_TYPE_X86`. **Neither has a case for the other arch, so both return 0 and exec fails `EBADARCH`.** This is the front door.
4. **`load_machfile` â†’ `parse_machfile`** (`mach_loader.c`) maps segments and, at line 1224, records `LC_LOAD_DYLINKER` into `dlp`, then line 1442 calls `load_dylinker(dlp, header->cputype, â€¦)`.
5. **`load_dylinker`** (`mach_loader.c:2960`) contains two facts that matter enormously:

```c
#if !(DEVELOPMENT || DEBUG)
	if (0 != strcmp(name, DEFAULT_DYLD_PATH)) {
		return LOAD_BADMACHO;
	}
#endif
	...
	cputype = (cputype & CPU_ARCH_MASK) | (cpu_type() & ~CPU_ARCH_MASK);
```

The release kernel **hard-refuses any dylinker but `/usr/lib/dyld`**, and it derives the dylinker's arch from *the main binary's* arch with the host's feature bits. Left alone, a foreign-arch main binary would make the kernel go looking for a foreign-arch slice of dyld â€” which would load and then immediately fail to execute. Both lines must be touched.

### 1.3 Where OpenOSX should intercept â€” recommendation

**Recommendation: kernel-side dylinker substitution, mirroring Apple. Do not use a userland launcher as the primary mechanism.**

Four concrete patch points, in dependency order:

**(a) `grade_binary` â€” return a low-but-nonzero grade for translatable foreign arch.**

This is the subtle one and it is worth getting right the first time. The grade must be **nonzero** (or exec fails outright) but **strictly lower than every native grade** (or a universal binary would prefer its foreign slice over its native one). Native arm64 grades in our tree run 9â€“11; native x86_64 grades run 1â€“3. So:

```c
/* bsd/dev/i386/kern_machdep.c â€” x86_64 host, arm64 guest */
case CPU_TYPE_ARM64:
    if (!openosx_translation_available(CPU_TYPE_ARM64))
        return 0;
    /* Strictly below CPU_TYPE_X86 (1) so fat binaries always prefer native. */
    return 0;  /* -- see note -- */
```

Note the collision: x86_64's native grades already start at 1, leaving no room below. Two options â€” (i) renumber the x86_64 native grades upward (`X86_64_H`â†’13, `X86_64_ALL`â†’12, `X86`â†’11) and give arm64 grade 1..2; or (ii) leave grading alone and instead handle the foreign case *after* the `grade` label by catching `EBADARCH`. **Option (i) is cleaner** and is almost certainly what Apple did on the ARM side, since arm64's grades were pre-spaced at 9/10/11 with room below for exactly this. Mirror that: give x86_64 hosts the same 9/10/11 spacing for native and 1/2 for translated arm64.

**(b) Set `is_cambria` and flag the proc.** In `exec_mach_imgact`, immediately after the grade check:

```c
if (imgp->ip_origcputype != (cpu_type() & ~CPU_SUBTYPE_MASK)) {
        imgp->ip_flags |= IMGPF_TRANSLATED;   /* new flag */
}
```
and in `parse_machfile`/`load_machfile`, propagate to `result->is_cambria = 1`. This automatically activates the already-present `vmkf_translated_allow_execute` path at `mach_loader.c:1909` (so foreign `__TEXT` maps executable without tripping code-signing enforcement) and the already-present `kern_exec.c:1503` stack setup (`load_result.dynlinker || load_result.is_cambria` â€” Apple wrote that `||` for exactly this reason).

**(c) Implement `proc_is_translated()`.** Replace the `kern_proc.c:2212` stub with a real read of a new `P_TRANSLATED`-style bit. Note `P_TRANSLATED 0x00020000` **already exists** at `proc.h:189` (marked `/* xxx */`, a leftover from Rosetta 1/PowerPC). Reuse it. This immediately makes `ubc_subr.c:4556/4618` behave correctly and gives you `sysctl sysctl.proc_translated` for free â€” the standard way apps and shell scripts detect translation.

**(d) Redirect the dylinker in `load_dylinker`.** Before the `DEFAULT_DYLD_PATH` strcmp:

```c
if (result->is_cambria) {
        name = OPENOSX_TRANSLATOR_PATH;      /* /usr/libexec/openosx/translate */
        cputype = cpu_type();                /* force NATIVE â€” never the guest arch */
}
```
and widen the release-mode strcmp to accept either `DEFAULT_DYLD_PATH` or the translator path. Forcing `cputype = cpu_type()` is essential; without it the `(cputype & CPU_ARCH_MASK) | ...` line two statements later would request a foreign-arch translator.

**Why not a pure-userland launcher?** A `binfmt_misc`-style userland shim (the approach Apple uses for *Rosetta on Linux VMs*, where the Linux kernel has no Darwin loader to hook) is viable as a **T0/T1 bring-up scaffold** and I recommend it for exactly that. But it cannot be the endpoint, for four reasons:

- `execve()` of a foreign binary must *succeed with the correct pid* â€” a re-exec wrapper breaks pid identity, and anything that `fork`/`exec`s and then `waitpid`s (every shell, `make`, launchd) sees the wrapper's exit semantics, not the app's.
- `posix_spawn` with `psa_binprefs` (`kern_exec.c:1053`) is how launchd and `arch(1)` request a specific slice. A userland shim can't participate.
- Shebang/`exec_shell_imgact` interactions and setuid semantics get ugly fast.
- You'd give up `vmkf_translated_allow_execute`, so the translator's JIT pages would fight code-signing enforcement.

The pragmatic sequencing: **build the translator against a userland launcher first** (`/usr/libexec/openosx/translate ./foo.arm64`), because it needs no kernel rebuild and iterates in seconds. Move the hook into the kernel once the translator itself is correct. The translator binary is *identical* either way â€” only who invokes it changes.

---

## 2. Foreign-arch system libraries on disk

### 2.1 How macOS does it

macOS ships **universal system binaries**: `/usr/lib/libSystem.B.dylib` contains both slices in one file, and there are **two dyld shared caches** â€” `dyld_shared_cache_arm64e` and `dyld_shared_cache_x86_64` â€” in `/System/Library/dyld/`. The x86_64 cache is a genuine, Apple-compiled x86_64 artifact. Rosetta's `aot_shared_cache` is a *third* file that holds arm64 translations of the x86_64 cache's contents, keyed by address correspondence maps.

Critically: **there is no path remapping.** A translated x86_64 process asks for `/usr/lib/libFoo.dylib` and gets the x86_64 slice of that exact file. dyld's normal fat-slice selection does the work. No `DYLD_ROOT_PATH`, no sysroot, no `/emul/` prefix.

### 2.2 Two viable layouts for OpenOSX

**Layout A â€” Universal (recommended endpoint).** Every system dylib and framework is a fat Mach-O containing both slices at its canonical path.

```
/usr/lib/libSystem.B.dylib          fat: [x86_64, arm64]
/usr/lib/dyld                       fat: [x86_64, arm64]
/System/Library/Frameworks/Foundation.framework/Foundation
                                    fat: [x86_64, arm64]
/usr/libexec/openosx/translate      thin: NATIVE arch only
/var/db/openosx-aot/<sha>/<sha>.aot AOT cache (mode 0755, root-owned)
```

- Zero collision risk â€” one path, one file, dyld picks the slice.
- Matches macOS exactly, so any app or tool that reasons about paths is correct by construction.
- Cost: image size roughly doubles for the covered library set.

**Layout B â€” Sysroot (recommended for T1â€“T2 bring-up).** Native libs stay where they are; the foreign set lives under a prefix:

```
/usr/lib/libSystem.B.dylib                       native (thin)
/Library/OpenOSX/arm64/usr/lib/libSystem.B.dylib guest (thin arm64)
/Library/OpenOSX/arm64/usr/lib/dyld              guest dyld (thin arm64)
```

The translator then sets `DYLD_ROOT_PATH=/Library/OpenOSX/arm64` in the guest's environment before handing control to the guest dyld. **Our in-tree dyld already supports this** â€” `src/Libraries/dyld/upstream/src/dyld2.cpp:2120` handles `DYLD_ROOT_PATH` (and its alias `DYLD_PATHS_ROOT`), and `ImageLoaderMachO.cpp:1534-1543` correctly combines it with `LC_RPATH`. Two caveats found in the source: it warns and ignores non-absolute paths (`dyld2.cpp:2125`), and **it refuses to combine with `DYLD_IMAGE_SUFFIX`** (`dyld2.cpp:2529`) â€” so don't use `_debug` suffixed libs in a translated process.

Layout B is far better for iteration: you can rebuild the entire guest userland with `nix build .#image-arm64-full` and drop it in as a directory tree without touching a single native file. **Ship B first, migrate to A once stable.**

### 2.3 How the translated process resolves dylibs

This is the design decision with the most leverage, and I want to be direct about the tradeoff.

**Option 1 â€” Translate the guest dyld too (Rosetta's model).** The translator loads the *guest-arch* `dyld` as translated code and lets it do everything: parse `LC_LOAD_DYLIB`, search paths, bind symbols, run initialisers.

- **Pro:** Maximum fidelity. `dlopen`, two-level namespaces, weak linking, `LC_RPATH`/`@loader_path`/`@executable_path`, lazy binding, `dyld_shared_cache` â€” all just work, because the real implementation is running.
- **Pro:** The translator needs to know nothing about Mach-O linking. It only translates instructions.
- **Con:** dyld is the hardest possible first workload. It self-relocates before any libc exists, does raw syscalls, and manipulates its own page protections.

**Option 2 â€” Native loader, translated leaves.** Write a native-arch mini-loader in the translator that maps guest images and binds symbols itself.

- **Pro:** Bring-up is much easier; you can get a statically-linked hello-world running with no dyld at all.
- **Con:** You are reimplementing dyld, and you will get `@rpath` resolution or weak-symbol semantics subtly wrong forever.

**Recommendation: Option 2 for T0â€“T1, Option 1 as the target.** Structure the translator so the loader is behind an interface from day one, so swapping in "just translate real dyld" is a contained change. This is not hypothetical sequencing â€” translating dyld requires the instruction translator to already be correct on hairy self-modifying-ish code, so it *cannot* be first.

### 2.4 The commpage â€” do not skip this

A guest arm64 process on x86_64 OpenOSX will read the **arm64 commpage** for `gettimeofday`, `mach_absolute_time`, CPU capability bits, and active-CPU count. Apple built the mirror-image facility (`osfmk/arm/cpu_x86_64_capabilities.h`) and redacted the implementation. OpenOSX needs the reverse: an `osfmk/i386/cpu_arm64_capabilities.h` plus a synthesised arm64 commpage at `_COMM_PAGE64_BASE_ADDRESS` in translated processes, populated by the x86_64 kernel.

Set the translated bit. Apple defines it identically on both sides â€” `kIsTranslated 0x4000000000000000ULL` at `osfmk/i386/cpu_capabilities.h:89` and `x86_64_kIsTranslated` at the same value in the ARM header. Guest code (including Apple's own libc fast paths) checks it.

---

## 3. The syscall boundary

### 3.1 Does it pass straight through?

**Yes â€” the syscall *numbers* and *semantics* are identical; only the *encoding* and *register placement* differ.** Both arches dispatch through the same `sysent` table generated from the same `syscalls.master`. There is no per-arch syscall numbering. This is the single largest advantage over Wine, and it is why the "OS emulation" half of the problem simply does not exist here.

But "straight through" needs three qualifications, and one of them is a genuine gotcha.

### 3.2 The encodings, verified from source

**Darwin x86_64** (`osfmk/mach/i386/syscall_sw.h:135-165`):

```c
#define SYSCALL_CLASS_SHIFT	24
#define SYSCALL_CLASS_MASK	(0xFF << SYSCALL_CLASS_SHIFT)
#define SYSCALL_CLASS_MACH	1	/* Mach */
#define SYSCALL_CLASS_UNIX	2	/* Unix/BSD */
#define SYSCALL_CONSTRUCT_UNIX(n)  ((SYSCALL_CLASS_UNIX << SYSCALL_CLASS_SHIFT) | ...)
```

- Number in `rax`, **class-tagged**: BSD = `0x2000000 | n`, Mach = `0x1000000 | n`, MDEP = `0x3000000 | n`.
- Args: `rdi, rsi, rdx, r10, r8, r9` (note `r10`, not `rcx` â€” the `syscall` instruction clobbers `rcx` with the return address). 7th+ on the stack.
- Instruction: `syscall`. Return in `rax` (+`rdx` for 64-bit pairs). **Error signalled by CF set.**

**Darwin arm64** (verified: `osfmk/arm64/proc_reg.h:1652` â†’ `#define ARM64_SYSCALL_CODE_REG_NUM (16)`):

- Number in **`x16`**, **untagged**. Args in `x0â€“x7` â€” `bsd/dev/arm/systemcalls.c:496` does a flat `memcpy(&uthread->uu_arg[0], &regs->x[indirect_offset], callp->sy_narg * sizeof(uint64_t))`, confirming a plain register copy with no munging on 64-bit.
- Instruction: `svc #0x80`. Return in `x0` (+`x1`). **Error signalled by C flag set** â€” `systemcalls.c:622` comments *"setting carry to trigger cerror call"*.
- **Mach traps are negative `x16` values.** `SYSCALL_CLASS_*` is defined **only** in the i386 header â€” a tree-wide grep finds no ARM definition â€” so ARM distinguishes classes by sign, not by a class field.
- Indirect syscall (`SYS_syscall`) is `x16 == 0`, which shifts args to `x1â€“x8` (`systemcalls.c:487`), with `x9` used for the 8-argument case.

### 3.3 What the translator must actually do at `svc #0x80` (arm64 guest on x86_64 host)

```
trap on SVC #0x80
  n := guest x16
  if n < 0:                      # Mach trap
      rax := 0x1000000 | (-n)
  elif n == 0:                   # indirect: real number is in x0
      rax := 0x2000000 | guest_x0 ; shift arg window by one register
  else:
      rax := 0x2000000 | n
  rdi,rsi,rdx,r10,r8,r9 := guest x0..x5
  args 7,8 (guest x6,x7) -> push to host stack
  execute host `syscall`
  guest x0 := rax ; guest x1 := rdx
  guest NZCV.C := host RFLAGS.CF          # <-- must not be forgotten
```

The **carry-flag propagation is the classic silent-corruption bug**: guest libSystem's `cerror` path keys off `C`, so if you drop it, every failing syscall silently looks like success with a garbage return value. Test it deliberately (`open("/nonexistent")` must set `errno`, not return a bogus fd).

### 3.4 Function-call ABI marshalling â€” where the real difficulty lives

The syscall boundary is easy. The **function-call boundary is not**, and there are Apple-specific divergences that generic AAPCS64 documentation will actively mislead you on.

| Concern | x86_64 SysV (Darwin) | arm64 (Apple) | Marshalling burden |
|---|---|---|---|
| Integer args | `rdi rsi rdx rcx r8 r9` | `x0â€“x7` | Trivial |
| FP args | `xmm0â€“7` | `v0â€“v7` | Trivial |
| Return | `rax`/`rdx` | `x0`/`x1` | Trivial |
| Large struct return | hidden ptr in `rdi` | **`x8`** | Easy but easy to forget |
| **Variadic args** | in registers; `al` = # vector regs | **Apple passes ALL variadic args on the stack** | **Hard.** Diverges from standard AAPCS64. Breaks every `printf`/`NSLog` if wrong. |
| **Narrow args** | widened to 32/64 bits | **Apple packs into low bits, does NOT widen** | **Hard.** Standard AAPCS64 says widen; Apple says don't. |
| `long double` | **x87 80-bit** | **64-bit (== `double`)** | **Hard.** Genuinely lossy; no correct answer. |
| `char` signedness | signed | signed (Apple; ARM std says unsigned) | Consistent â€” no work |
| Callee-saved | `rbx rbp r12â€“r15` | `x19â€“x28`, low 64 of `v8â€“v15` | Register allocator concern |
| Red zone | **128 bytes below `rsp`** | none | Host-side only; free in this direction |
| TLS base | `%fs` segment base | `TPIDRRO_EL0` | Map guest `TPIDRRO_EL0` reads to a host `%fs`-relative slot |
| Stack alignment | 16 at `call` | 16 | Consistent |
| Memory model | **TSO** | weakly ordered | **Free in this direction.** See Â§3.5 |
| Page size | 4K | 16K | Free in this direction |
| Pointer auth | n/a | arm64e only | Avoid â€” see Â§3.6 |

### 3.5 The memory-ordering asymmetry â€” state this plainly

Running an **arm64 guest on an x86_64 host**: the host is *stronger* than the guest requires. Guest `DMB`/`DSB` become no-ops; guest `LDAR`/`STLR` become plain `mov`; guest `LDXR`/`STXR` pairs map onto `cmpxchg`. **You cannot produce a memory-ordering bug by being too strong.** No barriers, no hardware support needed, correct on any x86-64 CPU.

Running an **x86_64 guest on an arm64 host**: the host is *weaker*. Every guest load and store must become `LDAR`/`STLR`, or you must have hardware TSO. Rosetta 2's performance is substantially attributable to the M1's undocumented TSO mode; on a generic ARM SoC this direction costs enormously.

**Implication for planning:** the arm64-on-x86_64 direction should be built first not merely because the user wants it more, but because it is the one that is correct-by-construction on commodity hardware.

### 3.6 arm64 vs arm64e â€” a constraint that works in our favour

`grade_binary` on ARM (`bsd/dev/arm/kern_machdep.c`) grades `CPU_SUBTYPE_ARM64E` via `grade_arm64e_binary(execfeatures)`, and `kern_exec.c:1098` gates `IMGPF_NOJOP` on `arm64_cpusubtype_uses_ptrauth`. Translating arm64e would mean emulating Pointer Authentication â€” signing/authenticating with per-process keys, `PACIA`/`AUTIA`, and the diversity values.

**Don't.** On macOS, arm64e is reserved for platform binaries and kexts; third-party apps ship plain `arm64` (`CPU_SUBTYPE_ARM64_ALL` / `_V8`). Have the translator explicitly reject arm64e slices with a clear diagnostic, and pick the `arm64` slice from any fat binary that has both. This removes an entire subsystem from scope at essentially no compatibility cost.

---

## 4. objc / Foundation â€” the mixed-arch constraint

### 4.1 Confirmed: a process is one architecture, entirely. No exceptions.

This is not a Rosetta policy choice; it is forced by the object model. Our own in-tree objc4 proves it.

**Tagged pointer encodings are mutually incompatible** (`src/Libraries/objc4/runtime/objc-internal.h:400-470`):

```c
#if __arm64__
#   define OBJC_SPLIT_TAGGED_POINTERS 1     // tag in HIGH bit, extended tags high
#else
#   define OBJC_SPLIT_TAGGED_POINTERS 0
#endif

#if (TARGET_OS_OSX || TARGET_OS_MACCATALYST) && __x86_64__
    // 64-bit Mac - tag bit is LSB
#   define OBJC_MSB_TAGGED_POINTERS 0
```

So on macOS x86_64, `_OBJC_TAG_MASK` is `1UL` (bit 0). On arm64 it is `(1UL<<63)` with a split high/low scheme, `_OBJC_TAG_EXT_SLOT_SHIFT 55`, and payload obfuscation. **The same `NSNumber` is a different bit pattern on each side.**

**`isa` packing differs** (`src/Libraries/objc4/runtime/isa.h`): `ISA_MASK` is `0x0000000ffffffff8` on arm64 vs `0x00007ffffffffff8` on x86_64 â€” different bitfield layouts in the non-pointer isa, meaning even reading an object's class requires knowing which arch created it.

**`objc_msgSend` is hand-written per-arch assembly** â€” `src/Libraries/objc4/runtime/arm64-asm.h:140,197` embeds `and $0, $1, #ISA_MASK` directly in the dispatch macros. There is no arch-neutral entry point to bridge to.

Add to this: different struct layouts (`long double`), different `va_list` representations, different method-cache mask/shift schemes. **Any attempt to have arm64 objc objects and x86_64 objc objects coexist in one address space is unsound at the level of pointer representation, before you even reach dispatch.**

### 4.2 What this means concretely

- **A translated app translates *everything* in its process**, including CoreFoundation, Foundation, objc4, and libSystem â€” all from the guest-arch slices. `objc_msgSend` runs as translated guest code performing translated dispatch on translated objects.
- **There is no thunking layer, and there should not be one.** FEX-Emu's model of forwarding OpenGL/Vulkan calls to native host libraries works because those are C ABIs with flat, marshallable signatures. objc/Foundation is not â€” you'd be marshalling an open-ended object graph.
- **Cross-arch communication happens only through serialisation boundaries**, all of which are arch-neutral by design and already present in the tree: XPC (`src/Libraries/XPC`), Mach IPC/MIG, UNIX domain sockets, files, `NSKeyedArchiver` plists.
- **Plug-in architectures inherit the constraint.** Apple's guide is explicit: a user may run the *whole host app* under Rosetta "to load a plug-in that has no native arm64 variant." The host process re-execs in the plug-in's arch; the plug-in does not get bridged. OpenOSX must adopt the same rule.

**Design consequence:** a mixed desktop is fine â€” some apps native, some translated, talking over XPC. A mixed *process* is impossible. Document this as a hard invariant before anyone tries to be clever with a bridging dylib.

---

## 5. Universal / fat binaries â€” build and ship

### 5.1 Format facts relevant to us

- Header magic `FAT_MAGIC` (`0xcafebabe`), **always big-endian** regardless of slice endianness. `FAT_MAGIC_64` (`0xcafebabf`) with 64-bit offsets when any slice exceeds 4 GB â€” needed for the shared cache, probably not for individual dylibs.
- Each `fat_arch` has `cputype`, `cpusubtype`, `offset`, `size`, `align` (as a power of 2). **Slice alignment must be `2^14` (16K) for arm64 and `2^12` (4K) for x86_64.** Getting arm64's alignment wrong produces a file that loads on a 16K-page host and fails mysteriously on a 4K one, or vice versa.
- Kernel side: `exec_fat_imgact` (`kern_exec.c:717`) â†’ `mach_fat.c:114` grades every slice; `get_macho_vnode` (`mach_loader.c:3516`) uses `fatfile_getbestarch_for_cputype(cputype, CPU_SUBTYPE_ANY, ...)` for the dylinker.

### 5.2 Tooling â€” already in the tree

`flake.nix:185-189` builds `cctoolsBuild` with `lipo_selfhost` among its targets. **`lipo` is already available.** Also present: `tools/cctools/libmacho/arch.c` and `include/mach-o/arch.h` (`flake.nix:140-142`), giving programmatic `cpu_type`â†”name mapping for the translator and any build tooling.

### 5.3 Recommended build integration

The tree already produces complete per-arch userlands: `arm64CrossToolchain` targeting `arm64-apple-darwin20.4` / `arm64-apple-macosx11.0` (`flake.nix:85-88`), and the `nix/arm64.nix` package set exports a *large* set of `*Arm64Build` derivations â€” `foundationArm64Build`, `gtk3Arm64Build`, `mesaArm64Build`, `netsurfArm64Build`, `opensshArm64Build`, `i3Arm64Build`, and dozens more (`flake.nix:2669-2712`). **The hard part â€” a complete dual-arch userland from one source tree â€” is already done.** That is the thing that makes this whole plan realistic rather than aspirational.

Add a Nix helper:

```nix
mkUniversal = name: { x86_64, arm64 }: pkgs.runCommand "${name}-universal" {} ''
  mkdir -p $out
  for f in $(cd ${x86_64} && find . -type f); do
    if file "${x86_64}/$f" | grep -q Mach-O && [ -f "${arm64}/$f" ]; then
      ${cctoolsBuild}/bin/lipo -create \
        -arch x86_64 "${x86_64}/$f" -arch arm64 "${arm64}/$f" \
        -output "$out/$f"
    else
      cp -a "${x86_64}/$f" "$out/$f"
    fi
  done
'';
```

**Practical guidance:** don't universalise the whole image. Size doubles for no benefit on binaries nothing translated will ever load. Universalise the **dylib closure a translated app actually needs** â€” libSystem, dyld, objc4, CoreFoundation, Foundation, XPC, libc++/libc++abi, CommonCrypto, Security, IOKit â€” and ship everything else thin.

**Do not universalise the kernel or kexts.** `is_cambria` and the shared-region patches are host-arch-specific kernel code; the kernel is thin by definition.

---

## 6. Phased plan T0 â†’ T4

Each phase has a milestone you can *demonstrate*, not just claim.

### T0 â€” Prove the fat/exec plumbing, no translation at all
**Goal:** the kernel correctly recognises, grades, and *refuses* a foreign binary with a specific diagnostic â€” and correctly *prefers* the native slice of a universal binary.

- Build `hello.c` for both arches from the existing toolchains; `lipo -create` them.
- Verify `lipo -info`, slice alignment (14 for arm64, 12 for x86_64).
- Patch `grade_binary` on x86_64 to renumber native grades to 9/10/11 and return 1 for `CPU_TYPE_ARM64`.
- Add `IMGPF_TRANSLATED` / `result->is_cambria` plumbing and implement `proc_is_translated()` using the existing `P_TRANSLATED` bit.
- Redirect `load_dylinker` to a **stub** translator that just prints its argv and exits 42.

**Milestone:** `./hello.universal` runs the x86_64 slice natively. `./hello.arm64` exits 42 with the stub's message. `sysctl sysctl.proc_translated` returns 1 inside it. **Zero translation code written; the entire OS-integration risk is retired.**

### T1 â€” Translated static hello-world, userland launcher
**Goal:** actually execute arm64 instructions.

- Build the translator as a native x86_64 binary. Start with a **straight interpreter** â€” no JIT. Correctness first; speed is a T3 problem.
- Mach-O loader for the guest (Option 2 from Â§2.3, behind an interface).
- Implement the `svc #0x80` bridge per Â§3.3, including carry-flag propagation.
- Synthesise the arm64 commpage; set `kIsTranslated`.
- Guest built `-static` against a minimal libSystem, or freestanding with raw syscalls.

**Milestone:** `/usr/libexec/openosx/translate ./hello.arm64` prints "hello" and exits 0. **Deliberately test that `open("/nonexistent")` sets `errno` correctly** â€” that is the carry-flag regression test and it will catch the single most likely silent bug.

### T2 â€” Dynamic linking against the real guest userland
**Goal:** a translated process running genuine arm64 libSystem/dyld built from this tree.

- Stage the guest userland under `/Library/OpenOSX/arm64` (Layout B), from `nix build .#image-arm64-minimal`.
- Patch `vm_shared_region.c` â€” remove the `#elif !defined(__arm__)` guard so an x86_64 host kernel can create an `SHARED_REGION_BASE_ARM64` region at `0x180000000` alongside its own at `0x7FFF00000000`. They don't overlap; the lookup already keys on `sr_cpu_type`.
- Switch to Option 1: translate the **real arm64 dyld**, with `DYLD_ROOT_PATH` set. Expect this to be the hardest debugging week of the project â€” dyld self-relocates before any libc exists.
- Wire the `TPIDRRO_EL0` â†’ host TLS mapping.

**Milestone:** a dynamically-linked arm64 `hello.c` that calls `printf` from the tree's own arm64 libSystem runs. Then: arm64 `sw_vers`, `ioreg`, and a shell.

### T3 â€” JIT, AOT cache, and objc/Foundation
**Goal:** usable speed, and real app frameworks.

- Replace the interpreter with a basic-block JIT. **Strong recommendation: evaluate `dynarmic` as the code generator** â€” it is an A64-guest â†’ x86-64-host dynamic recompiler under **0BSD** (maximally permissive, no attribution requirement, unambiguously compatible with an APSL/BSD image). It is archived upstream but actively forked, and its A64â†’x86-64 path is exactly the primary direction needed here. This is a genuinely lucky fit; the reverse direction has no comparable permissive option.
- AOT cache at `/var/db/openosx-aot/<sha256(contents)>/<sha256(path)>/`, keyed on **both** contents and path (Apple's scheme â€” path matters because `@executable_path` resolution differs).
- Translator uses `mmap(..., MAP_JIT | MAP_TRANSLATED_ALLOW_EXECUTE, ...)` â€” the flag is already accepted at `kern_mman.c:259`.
- Bring up arm64 objc4 + CoreFoundation + Foundation. Per Â§4, no bridging â€” all translated.
- Return-address prediction: maintain a shadow stack pairing guest return addresses with host ones (Rosetta's technique; large win on call-heavy objc code).

**Milestone:** an arm64 Foundation CLI tool (`NSString`, `NSArray`, `NSLog`) runs translated. Benchmark within ~3â€“5Ã— native; the interpreter will have been ~50Ã—.

### T4 â€” GUI apps, and the reverse direction
**Goal:** translated GUI apps on the desktop, plus x86_64-on-arm64.

- Universalise the core dylib closure (Layout A migration) via the `mkUniversal` helper.
- Kernel hook fully replaces the userland launcher; `posix_spawn` binprefs honoured so `arch -arm64 <cmd>` works.
- An `arch(1)` equivalent and a `sysctl.proc_translated`-aware `uname`.
- Translated arm64 GUI app against the tree's arm64 GTK3/Mesa stack.
- **x86_64-on-arm64** (classic Rosetta direction): mirror everything, but budget for the two hard parts â€” implement `x86_64_sharedpage_rw_addr` per the existing `osfmk/arm/cpu_x86_64_capabilities.h` (including `x86_64_kVmPageShift` for the 16Kâ†’4K page-size lie), and accept that without hardware TSO you must emit `LDAR`/`STLR` for every guest access. Expect materially worse performance than the forward direction. This is why it is T4 and not T2.

---

## 7. Licensing summary for anything shippable

| Component | License | Ship in APSL/BSD image? |
|---|---|---|
| XNU patches (`grade_binary`, `is_cambria`, shared region, commpage) | APSL 2.0 (derivative) | Yes â€” same license as the rest of the kernel |
| `dynarmic` (A64â†’x86-64 recompiler) | **0BSD** | **Yes â€” ideal.** No attribution burden at all |
| cctools / `lipo` | APSL 2.0 | Yes â€” already in-tree and built |
| dyld, objc4, CoreFoundation, libSystem (both arches) | APSL 2.0 | Yes â€” already in-tree |
| FEX-Emu | MIT | Compatible, but x86â†’arm only, and Linux-syscall-coupled. Reference for its thunking design, not a dependency |
| box64 | MIT | Compatible; x86â†’arm only; Linux-specific loader. Reference only |
| QEMU TCG / Unicorn | **GPLv2** | **Avoid linking.** Would make the translator GPL. Fine as an out-of-tree debugging oracle, never as a shipped component |
| Apple Rosetta binaries, `aot_shared_cache`, leaked sources | Proprietary | **Never.** Not obtained, not read, not redistributed |

**Clean-room note:** everything above derives from Apple's public Platform Security guide, FFRI's published reverse-engineering (Project Champollion), dougallj's public analysis, and **XNU source Apple itself released under APSL**, which is already vendored in this repository. The `is_cambria` bit, `MAP_TRANSLATED_ALLOW_EXECUTE`, `proc_is_translated`, and `cpu_x86_64_capabilities.h` are all in Apple's *own open-source release* â€” using them is not reverse engineering, it is using a published API surface for its evident purpose.

---

## 8. Files to touch â€” quick index

| File | Change |
|---|---|
| `src/Kernel/xnu/bsd/dev/i386/kern_machdep.c:55` | Renumber native grades to 9/10/11; add `CPU_TYPE_ARM64` â†’ 1 |
| `src/Kernel/xnu/bsd/dev/arm/kern_machdep.c:76` | Add `CPU_TYPE_X86_64` â†’ low grade (T4) |
| `src/Kernel/xnu/bsd/kern/kern_exec.c:1081` | Set `IMGPF_TRANSLATED` when `ip_origcputype != cpu_type()` |
| `src/Kernel/xnu/bsd/kern/mach_loader.c:2960` | `load_dylinker`: redirect `name` to translator, force `cputype = cpu_type()`, widen release-mode strcmp |
| `src/Kernel/xnu/bsd/kern/mach_loader.c:~1440` | Set `result->is_cambria` |
| `src/Kernel/xnu/bsd/kern/kern_proc.c:2212` | Implement `proc_is_translated()` via existing `P_TRANSLATED` (`proc.h:189`) |
| `src/Kernel/xnu/osfmk/vm/vm_shared_region.c:654` | Remove `#elif !defined(__arm__)` guard so x86_64 hosts can create ARM64 shared regions |
| `src/Kernel/xnu/osfmk/i386/cpu_arm64_capabilities.h` | **New** â€” mirror of the existing `osfmk/arm/cpu_x86_64_capabilities.h` |
| `flake.nix` | Add `mkUniversal`; add `translatorBuild`; stage `image-arm64-*` output under `/Library/OpenOSX/arm64` |

---

## Sources

- [Apple Platform Security â€” Rosetta 2 on a Mac with Apple silicon](https://support.apple.com/en-sg/guide/security/secebb113be1/web)
- [Project Champollion Part 1 â€” Analyzing AOT files and the Rosetta 2 runtime (FFRI)](https://ffri.github.io/ProjectChampollion/part1/)
- [Project Champollion Part 2 â€” Rosetta 2 runtime and AOT shared cache (FFRI)](https://ffri.github.io/ProjectChampollion/part2/)
- [FFRI/ProjectChampollion on GitHub](https://github.com/FFRI/ProjectChampollion)
- [dougallj â€” Why is Rosetta 2 fast?](https://dougallj.wordpress.com/2022/11/09/why-is-rosetta-2-fast/)
- [dynarmic â€” ARM dynamic recompiler (0BSD)](https://github.com/lioncash/dynarmic)
- [FEX-Emu](https://github.com/FEX-Emu/FEX) and [FEX ThunkLibs README](https://github.com/FEX-Emu/FEX/blob/main/ThunkLibs/README.md)
- [Box86/Box64 vs QEMU vs FEX vs Rosetta 2](https://box86.org/2022/03/box86-box64-vs-qemu-vs-fex-vs-rosetta2/)
- [Things You Might Want to Know About Apple's Rosetta 2 for Linux VMs](https://blog.inoki.cc/2026/02/28/Apple-Rosetta-Linux-VM-Secret-en/)
- [How Rosetta complicates call chains on M1 Macs â€” Eclectic Light](https://eclecticlight.co/2021/01/27/how-rosetta-complicates-call-chains-on-m1-macs/)
- In-tree XNU 20.5, objc4, dyld, and `flake.nix` under `/root/openosx` (paths cited inline)
