# OpenOSX Multi-Architecture Plan

**Part A â€” arm64 OpenOSX. Part B â€” cross-architecture binary translation.**
Companion documents: `docs/MACOS_COMPAT.md` (app compatibility), `docs/AQUA_UI_PLAN.md` (GUI stack), `docs/ROADMAP.md`.

---

## 1. Executive summary

OpenOSX starts this problem from a position almost nobody else has: **arm64 already builds in this tree** â€” a real `ARM64_BOARD_CONFIG_VIRT` XNU with PL011/GICv3 support, an AArch64 UEFI loader that builds Apple boot_args and `eret`s into EL1, two ARM platform kexts, four image targets â€” and **the same source tree produces a complete userland for both architectures**, so a translator can execute foreign-arch Mach-O against genuine foreign-arch libSystem, dyld, objc4, CoreFoundation and Foundation built from the same commit. That second property is exactly how Rosetta 2 works, and Apple needed a decade of dual-toolchain maintenance to get it; we get it as a side effect of `nix/arm64.nix`. Because guest and host are both Darwin with a shared `syscalls.master`, translation reduces to ISA translation plus register-shuffle ABI marshalling â€” there is no OS or API emulation layer, which is the entire cost of Wine and most of the cost of Darling. The primary direction we care about, arm64 guest on x86_64 host, is also the *technically easier* one: a strong-memory host running a weak-memory guest needs no barriers and no hardware TSO crutch, which is the thing Apple had to add silicon for. The honest counterweights are that arm64 does not currently boot to a shell (four specific, fixable defects, catalogued below), that translation buys nothing for GUI apps until the AppKit work in `docs/AQUA_UI_PLAN.md` lands, and that Apple Silicon bare metal is a multi-year project that should stay off the roadmap. The single highest-value near-term outcome is neither of those: it is running the arm64 userland's own test suite under translation on free x86_64 CI, which closes the arm64 bring-up loop with no ARM hardware at all.

---

# Part A â€” arm64 OpenOSX

## A.1 What actually exists today

Verified against the tree on `openosx-next`, not from memory.

| Layer | State |
|---|---|
| **Board config** | `pexpert/pexpert/arm64/QEMUVIRT.h` â€” PL011 @ `0x09000000` IRQ 33, GICv3 dist `0x08000000` / redist `0x080a0000`, RAM base `0x40000000`, 16K pages, generic timer. Wired at `board_config.h:233` as `ARM64_BOARD_CONFIG_VIRT` (`MAX_CPUS 4`). Real code paths in `pexpert/arm/pe_serial.c` and `pe_identify_machine.c`. |
| **Kernel config** | `config/MASTER.arm64.virt` â€” embedded flavour (`config_embedded`, `xsmall`, `CONFIG_VNODES=1024`), signed-code enforcement deliberately dropped. Selected by `-DOPENOSX_ARM64_MACHINE_CONFIG=VIRT`; **default if unset is BCM2837**, which is dead weight. |
| **Kernel targets** | `kernelArm64VirtBuild` (RELEASE), `kernelArm64VirtDebugBuild`, `kernelArm64Build` (BCM2837 â€” effectively dead). |
| **Kexts** | `kextsArm64Build` builds 29. ARM-specific: `PDArmPlatformExpert` (`IODTPlatformExpert`, GIC init, creates exactly **one** `PDArmCPU`) and `PDArmPCI` (ECAM `IOPCIBridge`, hardcodes `0x4010000000` + MMIO window `0x10000000+0x30000000`). Shared: `IOPCIFamily`, `IOStorageFamily`, `RavynAHCIPort`, `IONVMEFamily`, `IOVirtIONet/GPU`, USB stack, `hfs`/`apfs`/`ext4`/`msdosfs`, `corecrypto`, `pthread`. |
| **Boot chain** | AAVMF â†’ `BOOTAA64.EFI` (`tools/xnu-loader`, `-DXNU_LOADER_QEMU_VIRT=ON`) â†’ XNU at EL1. `src/jump.S` handles both EL2 and EL1 entry; `boot.c:arm64_boot_build_args()` builds ARM boot_args; `devtree.c` emits `dram-base`/`dram-size`, an empty static TrustCache, `/defaults`, `/cpus/cpu@0` with `timebase-frequency` from `CNTFRQ_EL0`, `/arm-io` and `/pci`. Root node `compatible`/`model` = `"ACPI"` so the platform-expert match fires. |
| **Userland** | Full arm64 cross-build: libSystem+dyld, icucore, libc++/libc++abi, libobjc, CoreFoundation, Foundation, IOKit, Security, launchd, launchctl, zsh, toybox â€” plus in the `-full` set X11/Xorg, i3, GTK3, Pango/Cairo/HarfBuzz, Mesa, NetSurf, Python, git, OpenSSH (~130 pkgs). Note **XFCE is x86-only**; arm64 "full" means i3. |
| **Images** | `image-arm64-virt`, `-minimal`, `-minimal-release`, `-full`. |

**Verified buildable.** `nix build --dry-run .#image-arm64-virt-minimal-release` resolves to 36 remaining derivations (the shared host toolchain is already cached by the x86 build) and roughly 1.5â€“3 h wall-clock on 12 threads.

## A.2 The four defects that stop it booting

| # | Defect | Consequence |
|---|---|---|
| **D1** | `arm64-virt-runner` attaches root as `-device virtio-blk-pci` (`nix/image-contents.nix:~970`) but **no VirtIO block driver exists in the tree** â€” `IOVirtIOFamily` ships only `IOVirtIOTransport`, plus `IOVirtIONet` and `IOVirtIOGPU` | UEFI reads the ESP, XNU boots, no `IOMedia` ever appears, root never mounts. Fatal on its own. |
| **D2** | `image-arm64-virt` passes `baseSystem = splitBaseSystem` and `extraPackages = imageExtraPackages` â€” **the x86_64 userland** â€” with an arm64 KC. The runner auto-detects **only** this image | An arm64 kernel with an x86_64 `/sbin/launchd`. Guaranteed non-boot, and it is the default target. |
| **D3** | `kc-arm64.nix` links `IOVirtIOGPU.kext`, which requires `com.apple.iokit.IOGraphicsFamily`, but `kextsArm64Build` sets `enableIOGraphicsFamily = false` | Unresolved KC dependency, and **arm64 has no framebuffer path at all** â€” serial only. `image-arm64-virt-full` ships X11 with no display driver. |
| **D4** | `nix/pkgs/toolchain/kc-arm64.nix` hardcodes its own kext list instead of consuming `nix/lib/kc-kexts.nix` | Exactly the drift that file's own header warns about. The two KCs will diverge silently forever. |

Upstream context: PureDarwin PR #171 merged arm64 and then the maintainer went back to driving x86 to a working desktop. The loader-side arm64 work is fresh (Julyâ€“Aug 2026 commits: 16K paging, Mach-O max segments 32â†’256 because arm64 filesets need â‰¥110); the OS-side arm64 work has not moved since the merge. Nobody has publicly claimed arm64 boots to a shell. D1â€“D4 have the exact shape of "merged after the last manual test, then bit-rotted."

## A.3 Build and test from x86_64 Linux / WSL

```bash
cd /root/openosx

nix build -L .#image-arm64-virt-minimal-release   # START HERE â€” RELEASE kernel, coherent userland
nix build -L .#image-arm64-virt-minimal           # DEBUG kernel, verbose, much slower
nix build -L .#image-arm64-virt-full              # only after minimal boots
# DO NOT build .#image-arm64-virt â€” it is the broken x86-userland mix (D2)

nix build -L .#kernel-arm64-virt   # bisection targets
nix build -L .#kexts-arm64
nix build -L .#kc-arm64
```

**Do not use the committed runner** until D1 is fixed. Run QEMU directly against AHCI, which is the only block driver in the arm64 KC:

```bash
qemu-system-aarch64 \
  -machine virt,gic-version=3 \
  -cpu max,pauth-impdef=on,sve=off \
  -smp 1 -m 4096 -boot order=c,strict=on \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$AAVMF_CODE" \
  -drive if=pflash,format=raw,unit=1,file="$STATE/AAVMF_VARS.fd" \
  -drive if=none,id=system,file="$IMG",format=raw,snapshot=on \
  -device ich9-ahci,id=ahci0 -device ide-hd,drive=system,bus=ahci0.0,bootindex=1 \
  -device virtio-net-pci,netdev=net0 -netdev user,id=net0,hostfwd=tcp::2223-:22 \
  -serial mon:stdio -display none -no-reboot -no-shutdown
```

Every deviation from the committed runner is load-bearing:

- **`ich9-ahci` + `ide-hd`** â€” the only block driver present (the `arm64-uboot-runner` already does this; the virt runner should too).
- **`-smp 1`** â€” `PDArmPlatformExpert::start()` creates one `PDArmCPU`, the loader's DT has only `/cpus/cpu@0`, and there is no PSCI secondary bring-up. `-smp 4` gives you three CPUs XNU never starts.
- **`-cpu max` is mandatory, not a preference** â€” `QEMUVIRT.h` sets `__ARM_16K_PG__ 1`, and the 16 KB granule is not implemented by `cortex-a53/a57/a72`.
- **`pauth-impdef=on, sve=off`** â€” pure TCG speed. QEMU's architected QARMA PAuth emulation is very slow; SVE costs translation time for nothing.

Clone `tools/testing/boot-test.sh` to `boot-test-arm64.sh` with a **â‰¥1800 s timeout** and staged pass regexes: `arm64 boot_args @` (loader) â†’ `Darwin Kernel Version` (kernel) â†’ `OpenOSX PDArmPCI: probe` (IOKit â€” this `IOLog` already exists) â†’ `BSD root:` â†’ launchd banner.

**Performance, honestly.** Cross-ISA means pure TCG â€” there is no KVM escape on an x86 host. Expect 10â€“30Ã— slower than native ARM, worse during bring-up because early boot is dominated by MMIO (every PL011 byte is a TCG exit), TLB manipulation, and 16 KB-granule page walks. Estimates: `-minimal-release` with quiet args **2â€“6 min** to shell; `-minimal` (DEBUG + full verbose) **10â€“25 min**; `-full` with X11 on softpipe boots in 5â€“10 min and is miserable to interact with. Use `-serial file:` in CI so terminal I/O is out of the loop.

## A.4 What a Mac adds, and the handoff to a Mac-side agent

**The step change is HVF.** `qemu-system-aarch64 -M virt,gic-version=3 -accel hvf -cpu host` runs guest EL1/EL0 natively on the M-series core. Boot drops to seconds; the arm64 loop becomes as fast as the x86+KVM loop. HVF on Apple Silicon supports GICv3 only â€” the flake already uses `gic-version=3`. Guest 16 KB granule is the *good* case on Apple cores. `jump.S` already handles the EL1-entry path AAVMF-under-HVF produces.

**But the Mac cannot build the image today.** `mkArm64Build` is literally `if isDarwin then null else â€¦` (`nix/arm64.nix:978`), and every image target lives in the `isDarwin`-gated `linuxPackages`/`linuxApps` (`flake.nix:3306-3307`). `image.nix` additionally carries `meta.platforms = platforms.linux` and needs `sgdisk`/`mkfs.ext4`/`mtools`/`apfsprogs`.

> **Handoff note for a Mac-side agent instance.** Your job is *test*, not build. Do not attempt `nix build .#image-arm64-*` on macOS â€” those attributes do not exist there and the failure is a gating bug, not a toolchain problem. The image `.img` is produced in WSL and copied over. On the Mac: use Homebrew `qemu` and its bundled `share/qemu/edk2-aarch64-code.fd` + `edk2-arm-vars.fd` (not the nixpkgs `pkgsCross` AAVMF), run with `-accel hvf -cpu host` and otherwise the exact device topology in Â§A.3, and report serial output against the same staged regexes. If you are asked to make the Mac a *build* node, that is phase A3 step 3 â€” un-gating `isDarwin` in `nix/arm64.nix`, `flake.nix` and `image.nix` â€” and it should not be attempted before VMAPPLE (Â§A.6) justifies the effort. A second, high-value Mac-only job is **differential ISA testing for Part B**: run identical instruction sequences natively and under `oxtrans`, diff register/flag/memory state. That turns the FP-semantics and flag-polarity work from guessing into engineering.

## A.5 Phases A0 â†’ A3

**A0 â€” Unblock and prove the boot chain. 1â€“2 weeks.**
Fix D1 (AHCI, `-smp 1`, `-cpu max,pauth-impdef=on,sve=off`); fix D2 (repair or delete `image-arm64-virt`, reorder runner auto-detect to prefer `-minimal-release`); fix D3 by dropping `IOVirtIOGPU` from the arm64 KC for now (serial-only is what A0 wants); fix D4 by making `kc-arm64.nix` consume `nix/lib/kc-kexts.nix` behind an arch predicate; strip the meaningless x86 framebuffer boot-args (`gopconsole=1 gen9_debug=1 vgpu_debug=1`); land `boot-test-arm64.sh`.
*Success:* serial shows loader banner â†’ `arm64 boot_args @` â†’ `Darwin Kernel Version 20.5.0` â†’ `PDArmPCI: probe`. **A panic after the Darwin banner still counts** â€” the boot chain is proven.

**A1 â€” Root device and PID 1. 2â€“6 weeks. This is the real milestone.**
Get `PDArmPCI` to enumerate the AHCI controller and `RavynAHCIPort` to publish `IOMedia`. The most likely first failure is a BAR outside the hardcoded `addBridgeMemoryRange(0x10000000, 0x30000000)` window â€” teach `devtree.c` to emit real `reg`/`ranges` on `/pci` and `PDArmPCI` to read them, instead of both sides hardcoding. Confirm the loader's `find_ext4_boot_uuid()` path and `Ext4FileSystemDriver`'s `boot-uuid-media`. Expect to re-fight arm64 versions of x86 bugs already fixed upstream â€” notably the ext4 locking bug that broke launchd â€” plus dyld chained-fixup issues on arm64 Mach-O.
*Success:* `BSD root:`, launchd banner, `zsh` prompt on serial. This is arm64's M2+M3+M4 equivalent in one phase.

**A2 â€” Usable CLI and the Mac-accelerated loop. 4â€“10 weeks.**
Bring up `IOVirtIONet` so `hostfwd tcp::2223-:22` reaches sshd â€” that converts the VM from serial toy to SSH-able build target. Ship the `.img` to the Mac and run under HVF; document that as the standard arm64 dev loop. Add arm64 CI: `nix build .#kernel-arm64-virt .#kexts-arm64 .#kc-arm64` on every push (cheap, catches bit-rot) plus a nightly image build and boot test â€” today the workflows contain **zero** arm64 references. Revisit `config_embedded` in `MASTER.arm64.virt`: x86 builds `[intel medium msgb_large]`, arm64 builds `[arm64 xsmall msgb_small config_embedded]`, and running the same macOS-flavoured userland over two kernel personalities will eventually bite (jetsam, task limits, `posix_spawn`, code-signing paths). **Part B makes this mandatory rather than tidy** â€” a translator marshalling two ABIs on one kernel must not have the kernel's personality depend on which slice is running. Optionally write `IOVirtIOBlock` (~500 lines on the existing transport; benefits x86 too).
*Success:* `ssh -p 2223 root@localhost` into arm64 OpenOSX; green arm64 CI; documented HVF loop.

**A3 â€” Graphics and VMAPPLE. 3â€“9 months, opportunistic.**
Restore `IOGraphicsFamily` for arm64, get `IOVirtIOGPU` working on `virtio-gpu-pci`, then Xvfb â†’ Xorg â†’ i3. Painful under TCG, fine under HVF â€” which is the argument for A2 preceding it. Then build a **VMAPPLE machine config**: `ARM64_BOARD_CONFIG_VMAPPLE` already exists at `board_config.h:243` with `MAX_CPUS 32` and `USE_APPLEARMSMP 1`, `pexpert/arm64/VMAPPLE.h` exists, and `pe_serial.c` already shares its UART path with QEMUVIRT. This targets Apple's Virtualization.framework â€” near-native arm64 OpenOSX on the user's own Mac with Apple's accelerated virtio devices, and a genuine differentiator: a fully open Darwin booting in Apple's own hypervisor. Un-gate `isDarwin` only once VMAPPLE justifies it.
*Success:* i3 on arm64 under HVF at usable framerates; OpenOSX boots as a Virtualization.framework guest.

## A.6 Apple Silicon bare metal â€” the honest answer

**Do not put this on the roadmap as a deliverable.** It is a multi-year, multi-person project.

Getting *any* code to run requires Permissive Security via 1TR + `bputil`, plus a per-OS "stub macOS" APFS container carrying **Apple's own iBoot2, firmware blobs and RecoveryOS** alongside your kernel â€” the platform enforces a 1:1 mapping between installed OS and OS-as-seen-by-the-machine. That is ironic and real: booting an all-open Darwin on Apple Silicon requires shipping Apple's closed boot chain on disk, which collides directly with the project's "no closed Apple binaries in the boot chain" rule. The only honest framings are "the user supplies it from their own machine" (Asahi's model) or "out of scope."

Getting anything *useful* then requires, from scratch, IOKit kexts for: **AIC** (not GIC â€” `PDArmGIC.cpp` is useless here), **DART/SART** IOMMUs, **ANS2 NVMe** (a coprocessor with an RTKit mailbox, not a spec-compliant controller, so `IONVMEFamily` does not apply), **RTKit** itself, **DCP/Apple Display Pipe**, **PMGR/SMC**, dwc3 USB + Type-C PD, SPI/IÂ²C HID, brcmfmac WiFi over a bespoke PCIe path, Bluetooth, audio. Asahi has been at this since 2020 with a funded team, using Linux's mature driver framework and DT bindings, and is still shipping progress reports in 2026 â€” and **their drivers are GPL, so under the BSD/ISC-only policy we cannot port them, only read their documentation.**

The two things worth doing instead are already named: **VMAPPLE** (A3) captures nearly all of the Apple-hardware performance benefit with none of the driver problem, and `ARM64_BOARD_CONFIG_T8103` existing in-tree means the kernel is not the blocker if anyone ever does show up. Set expectations as: **QEMU virt â†’ VMAPPLE guest â†’ (maybe, someday, with help) bare metal.**

---

# Part B â€” Cross-architecture binary translation

Working name: **`oxtrans`**.

## B.1 Why this is a smaller problem here than anywhere else

Wine emulates an OS. box64 hand-wraps host libraries with per-function ABI thunks. qemu-linux-user carries ~20k lines of syscall translation because every Linux architecture numbers its syscalls differently. **We need none of it**, for three compounding reasons: guest and host are both Darwin; Darwin generates syscall numbers from a single `syscalls.master` shared by all architectures, so `SYS_write` is 4 on both and Mach traps share their negative-number table; and this tree already builds genuine foreign-arch system dylibs from the same commit. The translated process runs *real* arm64 libSystem, dyld, objc4, CoreFoundation and Foundation â€” not shims.

The consequence for engine selection is decisive: **do not pick an engine for its library-thunking infrastructure.** Pick it for the quality of its ISA core, because the OS layer is the part we write and the part where our advantage lives.

## B.2 Engine decision

| Direction | Engine | License | Rationale |
|---|---|---|---|
| **arm64 â†’ x86_64 (primary)** | **dynarmic** â€” A64 frontend + x86-64 backend | **0BSD** | The only production-grade, permissively licensed arm64â†’x86-64 JIT in existence. Proven against hostile workloads (Switch game code, guest JITs, heavy multithreading). Crucially it is a *library that emulates a CPU and nothing else* â€” no ELF loader, no syscall layer, no rootfs model, **zero Linux coupling to rip out.** Deps are all permissive (xbyak BSD-3, zydis/fmt/robin-map MIT). |
| **x86_64 â†’ arm64 (secondary)** | **FEXCore** (from FEX-Emu) | **MIT** | Best IR and optimiser in the field, full SSEâ†’AVX2, actively developed, and `FEXCore` is cleanly separated from the Linux `LinuxEmulation`/`HLE` layers we would replace. |

**Rejected:** QEMU TCG and Unicorn (**GPLv2** â€” would make the translator GPL inside an APSL/BSD image; QEMU's `darwin-user` was also deleted upstream in 2012, so it is a 14-year-dead target). box64 (MIT, but its benchmark advantage comes entirely from native-library wrapping, which is precisely the technique we must not use; strip it and you have a less sophisticated JIT). Apple's Rosetta binaries â€” **architecture only, never bits.**

**The main risk to the dynarmic recommendation, stated plainly:** its A64 frontend was built for the Switch's Cortex-A57, broadly ARMv8.0-A. Apple-targeted code is ARMv8.5-ish and uses LSE atomics, FP16, dotprod and other v8.1â€“v8.5 additions the decoder may not cover. **Budget meaningful decoder extension work.** It is mechanical and additive (the decoder is table-driven), it is work we would do from scratch anyway, and it is a fraction of the 1â€“2 person-years a comparable JIT would cost. Use Capstone (BSD-3) as a cross-check decoder to drive exhaustive coverage tests. *Escape hatch:* an LLVM ORC AOT path (Apache-2.0 + LLVM exception) modelled on Rosetta's AOT design has a much higher ceiling â€” **design the IR/Darwin-layer boundary now so it can be swapped in later without touching the OS layer.**

## B.3 Kernel vs userland interception â€” decided

**Endpoint: kernel-side dylinker substitution, mirroring Apple. Bring-up: userland launcher.**

Apple's Platform Security guide states it exactly: for a translated process "the kernel transfers control to a special Rosetta translation stub rather than to the dynamic link editor, `dyld(1)`." The hook is in the Mach-O loader, and it is a dylinker substitution, not a re-exec wrapper.

**Our XNU 20.5 tree still contains the scaffolding, with the load-bearing parts redacted.** Apple's codename is *Cambria* and it survives: `load_result_t` carries `is_cambria : 1` (`bsd/kern/mach_loader.h:81`), `mach_loader.c:1909` consumes it to set `vmkf_translated_allow_execute`, and `kern_exec.c:1503` already reads `load_result.dynlinker || load_result.is_cambria` for stack setup. **Nothing in the open tree ever sets that bit.** That is the seam. Likewise `MAP_TRANSLATED_ALLOW_EXECUTE` is already accepted at `kern_mman.c:259`, `P_TRANSLATED` already exists at `proc.h:189` (a Rosetta-1 leftover), `proc_is_translated()` is a one-line stub at `kern_proc.c:2212`, and `ubc_subr.c` already special-cases translated processes for code-signing blob absence. Using these is not reverse engineering â€” they are in Apple's own APSL release, already vendored here.

Four patch points, in dependency order:

1. **`grade_binary`** (`bsd/dev/i386/kern_machdep.c:55`) â€” return a grade that is **nonzero** (or exec fails `EBADARCH`) but **strictly below every native grade** (or a universal binary prefers its foreign slice). x86_64's native grades currently start at 1, leaving no room; renumber them to 9/10/11 mirroring the ARM side's pre-spaced 9/10/11, and give translated arm64 grade 1â€“2. The ARM side's spacing is almost certainly Apple leaving room for exactly this.
2. **`exec_mach_imgact`** (`kern_exec.c:1081`) â€” set a new `IMGPF_TRANSLATED` when `ip_origcputype != cpu_type()`, propagate to `result->is_cambria` in `parse_machfile`.
3. **`proc_is_translated()`** â€” implement over the existing `P_TRANSLATED` bit. This makes `sysctl.proc_translated` work, which is how apps and scripts detect translation.
4. **`load_dylinker`** (`mach_loader.c:2960`) â€” when `is_cambria`, redirect `name` to `/usr/libexec/openosx/translate` **and force `cputype = cpu_type()`**. The forcing is essential: two lines later the existing `cputype = (cputype & CPU_ARCH_MASK) | (cpu_type() & ~CPU_ARCH_MASK)` would otherwise request a foreign-arch translator. Also widen the release-build strcmp that currently hard-refuses any dylinker but `/usr/lib/dyld`.

**Why a userland launcher cannot be the endpoint:** `execve` of a foreign binary must succeed with the *correct pid* (every shell, `make` and launchd waits on it); `posix_spawn` with `psa_binprefs` is how `arch(1)` and launchd request a slice and a shim cannot participate; shebang and setuid interactions get ugly; and you forfeit `vmkf_translated_allow_execute`, so JIT pages fight code-signing. **But build against the launcher first** â€” it needs no kernel rebuild and iterates in seconds. The translator binary is identical either way; only the invoker changes.

Also patch `osfmk/vm/vm_shared_region.c:654` â€” the `#elif !defined(__arm__)` guard compiles out the case we need. The machinery is already multi-arch: `vm_shared_region_lookup` keys on `sr_cpu_type`, and `SHARED_REGION_BASE_ARM64 = 0x180000000` vs `SHARED_REGION_BASE_X86_64 = 0x7FFF00000000` do not overlap. One kernel can host both concurrently.

**Code signing:** XNU verifies cdhashes on page-in and translated processes execute host code not covered by the guest signature. Apple solves this with kernel verification of the *original* pages plus a system-signed AOT. For OpenOSX (AMFI-relaxed, SIP off) the answer is: don't enforce. Note it and move on.

## B.4 Foreign-arch dylib staging

macOS ships universal system binaries at canonical paths with two shared caches, and does **no path remapping** â€” dyld's normal fat-slice selection does the work. Two layouts, and we want both in sequence:

**Layout B â€” sysroot. Use for T1â€“T3.** Native libraries stay put; the guest set lands under `/Library/OpenOSX/arm64/...` straight out of `nix build .#image-arm64-virt-minimal-release`. The translator sets `DYLD_ROOT_PATH` before handing control to the guest dyld. **Our in-tree dyld already supports this** (`dyld2.cpp:2120`, correctly combined with `LC_RPATH` at `ImageLoaderMachO.cpp:1534`). Two caveats found in the source: non-absolute paths are warned and ignored, and it **refuses to combine with `DYLD_IMAGE_SUFFIX`** â€” so no `_debug` libs in a translated process. Iteration cost is a directory drop with zero native files touched.

**Layout A â€” universal. The endpoint.** `lipo -create` both slices at canonical paths; matches macOS exactly so anything reasoning about paths is right by construction. `lipo_selfhost` is **already built** by `cctoolsBuild`. Slice alignment matters: `2^14` for arm64, `2^12` for x86_64. Add a `mkUniversal` Nix helper, but **universalise only the dylib closure a translated app needs** â€” libSystem, dyld, objc4, CoreFoundation, Foundation, XPC, libc++/libc++abi, Security, IOKit â€” not the whole image, and never the kernel or kexts.

**Loader strategy.** Option 2 (native mini-loader) for T1 because dyld is the hardest possible first workload â€” it self-relocates before any libc exists. Option 1 (**translate the real guest dyld**) is the target, because it buys two-level namespaces, weak binding, chained fixups, `@rpath`/`@loader_path`, ObjC image registration and the shared cache *for free*. Put the loader behind an interface on day one so the swap is contained.

**One process, one architecture â€” a hard invariant.** This is forced by the object model, not policy. Our own objc4 proves it: tagged-pointer encoding is bit-0 on x86_64 and split-high-bit on arm64 (`objc-internal.h:400-470`), so the same `NSNumber` is a different bit pattern; `ISA_MASK` is `0x0000000ffffffff8` vs `0x00007ffffffffff8` (`isa.h`), so even reading an object's class requires knowing which arch made it; and `objc_msgSend` is hand-written per-arch assembly with `ISA_MASK` embedded in the dispatch macros. **There is no bridging dylib and there must never be one.** Cross-arch communication happens only through XPC, Mach IPC/MIG, sockets, files and keyed archives â€” all arch-neutral and all already in the tree. Plug-ins inherit the rule: the *host app* re-execs in the plug-in's arch, exactly as macOS does. Write this down before someone tries to be clever.

## B.5 ABI and syscall marshalling

The syscall boundary is a register shuffle. At `svc #0x80` with an arm64 guest on an x86_64 host:

```
n := guest x16
if n < 0:        rax := 0x1000000 | (-n)          # Mach trap (ARM signals class by sign;
elif n == 0:     rax := 0x2000000 | guest_x0      #   SYSCALL_CLASS_* exists only in the
                 shift the arg window by one      #   i386 header â€” grep the tree, it's true)
else:            rax := 0x2000000 | n             # BSD
rdi,rsi,rdx,r10,r8,r9 := guest x0..x5             # r10 not rcx â€” `syscall` clobbers rcx
args 7,8 (guest x6,x7) -> host stack
execute host `syscall`
guest x0 := rax ; guest x1 := rdx
guest NZCV.C := host RFLAGS.CF                    # <-- DO NOT FORGET
```

**The carry flag is the classic silent-corruption bug.** Guest libSystem's `cerror` path keys off `C` (`bsd/dev/arm/systemcalls.c:622` literally comments "setting carry to trigger cerror call"). Drop it and every failing syscall looks like success with a garbage return. Make `open("/nonexistent")` setting `errno` a deliberate T1 regression test.

The **function-call** boundary is where the real difficulty is, and generic AAPCS64 documentation will actively mislead you on three Apple divergences:

| Concern | x86_64 SysV (Darwin) | arm64 (Apple) | Burden |
|---|---|---|---|
| Int / FP args, returns | `rdi rsi rdx rcx r8 r9`, `xmm0â€“7`, `rax`/`rdx` | `x0â€“x7`, `v0â€“v7`, `x0`/`x1` | trivial |
| Large struct return ptr | hidden in `rdi` | **`x8`** | easy to forget |
| **Variadic args** | in registers, `al` = vector count | **Apple passes ALL variadic args on the stack** | **hard** â€” diverges from standard AAPCS64; breaks every `printf`/`NSLog` if wrong |
| **Narrow args** | widened to 32/64 | **Apple packs into low bits, does not widen** | **hard** â€” standard AAPCS64 says widen |
| `long double` | x87 80-bit | 64-bit (== `double`) | **hard** â€” genuinely lossy, no correct answer |
| Red zone | 128 bytes below `rsp` | none | free in this direction |
| TLS base | `%fs` segment base | `TPIDRRO_EL0` | map guest reads to a host `%fs` slot |

**Also required:** a synthesised arm64 **commpage** at `_COMM_PAGE64_BASE_ADDRESS`, with `kIsTranslated` (`0x4000000000000000`) set â€” guest libc fast paths check it. Apple built the mirror-image facility for the other direction and shipped the header: `osfmk/arm/cpu_x86_64_capabilities.h` defines a complete x86_64 commpage for an ARM kernel to synthesise at `0x7fffffe00000`, including `x86_64_kVmPageShift` (the 16K-vs-4K page-size lie), with `x86_64_sharedpage_rw_addr` declared `extern` and defined nowhere. We need `osfmk/i386/cpu_arm64_capabilities.h`, its mirror. Also `CNTVCT_EL0` â†’ scaled TSC, and `IC IVAU`/`DC CVAU`/`ISB` â†’ translation-cache invalidation hints.

**Signals are the tarpit.** The host kernel delivers a host-arch `mcontext_t`; the translator must synthesise the guest-arch equivalent on the guest stack, resume at the guest handler, and unwind on `sigreturn`. This is where FEX and box64 both accumulated the most bugs. Budget real time.

**Skip arm64e.** Third-party macOS apps ship plain `arm64`; only Apple's own binaries are arm64e, and we build our own guest dylibs without `-mbranch-protection`, so the problem is sidestepped by construction. Emulating PAC *instructions* is easy (a keyed tweak, explicitly not a security boundary); the arm64e *ABI* â€” signed vtable entries, signed ObjC `isa`, signed block invoke pointers, each with discriminators â€” is a large fiddly subsystem for zero compatibility gain. Reject arm64e slices with a clear diagnostic and pick the `arm64` slice from fat binaries that carry both.

**Page size favours us.** arm64 Darwin uses 16K pages, x86_64 uses 4K; 16384 is an exact multiple of 4096, so guest `LC_SEGMENT_64` mappings need no fixup. Build the translation sysroot with `PAGE_MAX_SHIFT=12` â€” Darwin deliberately exposes `vm_page_size` as a runtime variable for exactly this, and we control the build in a way Apple never did. For third-party apps with 16K-baked allocators (jemalloc `--with-lg-page=14` ships in Chromium, Rust and Geekbench), have the `mmap`/`mach_vm_allocate` shim round and align guest allocations to 16K. ~30 lines, do it day one; retrofitting after chasing a jemalloc crash is miserable.

## B.6 Memory ordering â€” why the primary direction is the favourable one

Order the models: **SC âŠ x86-TSO âŠ ARMv8.** TSO forbids everything ARMv8 forbids and more. Therefore **every execution the x86 host can produce is a legal execution of the ARM program.** Soundness is by construction; plain guest loads and stores translate 1:1 with zero fences.

| arm64 guest | x86_64 host | Cost |
|---|---|---|
| `LDR`/`STR`, `LDAR`/`LDAPR` (acquire), `STLR` (release) | `mov` | free |
| `STLR` then `LDAR` (SC pair) | `xchg [m],r` or `lock or $0,(%rsp)` | one locked op, rare |
| `DMB`/`DSB` | `lock or $0,(%rsp)` (cheaper than `mfence`) | only where the guest asked |
| `ISB` | no-op + translation-cache invalidation hint | â€” |
| **LSE**: `LDADD`, `LDCLR`, `SWP`, `CAS`, `CASP` | `lock xadd`, `lock and`, `xchg`, `lock cmpxchg`, `lock cmpxchg16b` | near-perfect 1:1 |
| `LDXR`/`STXR` | pattern-match the loop â†’ `lock cmpxchg`; else emulate a monitor | the one wart |

LL/SC is less of a problem than it looks: `FEAT_LSE` is mandatory from ARMv8.1, every Apple core has it, and clang targeting `apple-m1` emits `CAS`/`LDADD`. Keep a monitor-emulation fallback (address + value snapshot, `STXR` â†’ `cmpxchg`), accept the ABA hazard, document it â€” dynarmic already ships exactly this deviation.

**The contrast is the whole argument.** Every x86 store is a release and every x86 load an acquire, so a naive x86â†’ARM translator must fence essentially every access. Risotto (ASPLOS'23), Lasagne (PLDI'22) and Arancini (ASPLOS'26) are three separate research programmes existing solely to minimise those fences with formally verified mappings; Risotto's payoff for all that machinery was 6.7%. Apple skipped software entirely and put a **TSO mode bit in the M1's memory subsystem** â€” measured at only ~9% over native weak ordering, versus multiples for software fencing. **We need none of it.** No fence optimiser, no proof obligation, no hardware crutch.

Two lesser asymmetries point the same way. **Register pressure runs against us** â€” 31 GPRs + 32 vector registers do not fit in 16+16, so guest state lives in a context struct behind a pinned host register with per-block allocation of the hot 4â€“6, and this is where the performance goes (dougallj's Rosetta analysis calls the reverse direction "less viable" for exactly this reason). Design the backend boundary so an **Intel APX** backend (32 GPRs) can drop in later. And ARM's carry is inverted relative to x86's borrow on subtract â€” carry a "polarity" bit in the IR so the optimiser elides most inversions. **Guest JITs run in our favour**: AArch64 requires explicit `IC IVAU`/`DSB`/`ISB` after writing code, so a correct guest *tells us* when it modified code; an x86 guest gives no such notification and forces write-protection detection. Keep a conservative write barrier for guests that cheat.

**Realistic performance: 25â€“50% of native** (2â€“4Ã— slowdown), worse than Rosetta 2's 71% precisely because of registers, and with GPU- and I/O-bound code unaffected. For the reverse direction on stock ARMv8 silicon, budget 25â€“50% too â€” Rosetta's 71% is not reproducible without Apple's TSO bit, `FEAT_AFP`, `FEAT_FlagM` and an undocumented flag-computation extension.

**The real hot path is indirect branches, not arithmetic.** `objc_msgSend`'s arm64 fast path ends in `br x17`, so *every message send is an indirect branch* and an ObjC UI does millions per second. Mitigate in order: inline 2-way branch-target caches; a return-address stack translating guest `BL`/`RET` into host `call`/`ret` so the host's return predictor stays accurate (Rosetta does this in the other direction); and **native fast-path overrides â€” the highest-leverage idea here.** Maintain a `symbol â†’ hand-written host-native implementation` table applied when the guest dyld binds, covering ~30 hot leaf functions: `objc_msgSend`/`objc_msgSendSuper2`, `objc_retain`/`release`/`autorelease`, `swift_retain`/`release`, `memcpy`/`memmove`/`memset`/`strlen`/`_platform_memmove`, `os_unfair_lock_*`, `dispatch_once`. **Because we own libobjc's and libdispatch's source, the guest data structures are documented-by-construction rather than reverse-engineered.** Keeping this to leaf functions with trivial signatures captures most of box64's benefit while avoiding the struct-ABI marshalling swamp that makes its whole-library approach so laborious.

## B.7 Phases T0 â†’ T4

**T0 â€” Prove the exec plumbing with zero translation code. 2â€“4 weeks.**
Build `hello.c` for both arches, `lipo -create`, verify slice alignment. Patch `grade_binary`, add `IMGPF_TRANSLATED`/`is_cambria`, implement `proc_is_translated()`. Redirect `load_dylinker` to a **stub** that prints argv and exits 42. Stage the arm64 sysroot at `/Library/OpenOSX/arm64` from `nix build .#image-arm64-virt-minimal-release`.
*Milestone:* `./hello.universal` runs the x86_64 slice natively; `./hello.arm64` exits 42; `sysctl sysctl.proc_translated` returns 1 inside it. **The entire OS-integration risk is retired before a line of translation is written.**

**T1 â€” Translated static hello-world under a userland launcher. 1â€“3 months.**
dynarmic core (or a straight interpreter first â€” correctness before speed), guest Mach-O loader behind an interface, `svc` bridge, commpage synthesis, 16K allocation shim. **Check the address-space collision on day one:** both arm64 and x86_64 Darwin main executables link at `0x100000000`, so `oxtrans` must be linked at an unusual base (`-image_base 0x200000000`) or be a dylib behind a stub, and must reserve the guest's canonical range before loading. Cheap to design around, expensive to retrofit.
*Milestone:* `/usr/libexec/openosx/translate ./hello.arm64` prints and exits 0, **and `open("/nonexistent")` sets `errno`** (the carry-flag test).

**T2 â€” Real guest dyld, real guest libSystem. 2â€“5 months.**
Remove the `vm_shared_region.c` guard. Switch to translating the actual arm64 dyld with `DYLD_ROOT_PATH` set. Wire `TPIDRRO_EL0` â†’ host TLS. Expect this to be the hardest debugging stretch of the project.
*Milestone:* a dynamically-linked arm64 binary calling `printf` from this tree's own arm64 libSystem runs; then arm64 `sw_vers`, `ioreg`, and a shell.

**T3 â€” JIT, AOT cache, objc/Foundation â€” and the milestone that pays for everything. 3â€“6 months.**
Basic-block JIT with `mmap(MAP_JIT | MAP_TRANSLATED_ALLOW_EXECUTE)`. AOT cache in Rosetta's shape: `/var/db/openosx-aot/<sha256(contents)>/<sha256(path)>/`, written `.in_progress` then atomically renamed â€” key on **both** contents and path, because `@executable_path` resolution differs. Bring up arm64 objc4 + CoreFoundation + Foundation, all translated, no bridging. **Then run the arm64 userland's own test suite under translation on x86_64 CI and diff against native results.**
*Milestone:* an arm64 Foundation CLI tool (`NSString`, `NSArray`, `NSLog`) runs within ~3â€“5Ã— native. And the arm64 conformance suite runs green on a free GitHub Actions x86 runner.

**T4 â€” Transparency, universalisation, and the reverse direction. 6â€“18 months.**
Kernel hook fully replaces the launcher; `posix_spawn` binprefs honoured so `arch -arm64 <cmd>` works; `arch(1)` and a translation-aware `uname`. Migrate the core dylib closure to Layout A via `mkUniversal`. Native fast-path overrides and benchmarking. Then **x86_64-on-arm64**: mirror the loader, syscall bridge and signal bridge (all shared), implement `x86_64_sharedpage_rw_addr` per the existing ARM header including `x86_64_kVmPageShift`, and accept that without hardware TSO you emit `LDAR`/`STLR` for every guest access.
*Milestone:* `./arm64binary` just works, with no wrapper and no special invocation.

**Share everything above the ISA core** â€” loader, syscall bridge, signal bridge, commpage, fast-path table, AOT format, kernel hook. That shared layer is ~70% of the total work and is exactly where the Darwin-on-Darwin advantage pays. Structure the repo with pluggable ISA backends from commit one.

---

## 4. Interaction with the app-compat and Aqua work

`docs/AQUA_UI_PLAN.md` Â§8 already names arm64-only apps as needing "a Rosetta-equivalent, i.e. a separate project of comparable size to this one." That is right, and the sequencing consequence is sharp: **an arm64-only Mac app needs BOTH translation and the GUI stack. Neither alone produces a running app.**

Concretely, a translated arm64 GUI app requires all of `docs/MACOS_COMPAT.md` C0â€“C4 *and* `docs/AQUA_UI_PLAN.md` G0â€“G5 *and* `oxtrans` T0â€“T4. The multiplication is honest: AQUA alone is calibrated at **2.5â€“4 years to "a real third-party app is usable"**, translation adds 1â€“2 years of largely parallel work, and the intersection is not additive but it is not free either.

Therefore:

1. **Do not sequence translation behind the GUI stack, or the GUI stack behind translation.** They are independent workstreams with one late convergence point (AQUA G4/G5 âˆ© oxtrans T3/T4). Run them in parallel, at whatever cadence the available attention allows.
2. **The first customer of `oxtrans` is OpenOSX itself, not app compatibility.** T3's "run the arm64 test suite under translation on x86_64 CI" closes the Part A bring-up loop with no ARM hardware, no QEMU-system boot flakiness and full debugger access. No other project in this space can do that, because no other project builds both slices from one tree. **Sequence T3 early and use it**; everything after is upside.
3. **Translation's own tier ladder mirrors `MACOS_COMPAT.md`'s and should reuse its harness.** Tier 0 (pure libc â€” `jq 1.6` imports 137 symbols from two dylibs) is the correct first *external* translation target and validates the whole loader/syscall path. Tier 1 (objc4/Foundation) is where T3 lands. Tier 2+ (Cocoa GUI) cannot be attempted before AQUA G4.
4. **`MACOS_COMPAT.md`'s Tier 3 targets are the strategic argument.** Sublime Text and BBEdit ship Universal 2 today, so they have x86_64 slices and need no translation â€” but the direction of travel is arm64-only, and every year that passes moves more of the corpus behind this wall. Translation is future-proofing, not a present-day unlock.
5. **State the T4 wall internally before anyone hits it.** The wall is not the ISA â€” dynarmic will chew through arm64 code fine. It is AppKit, Metal, CoreGraphics and CoreAnimation, i.e. the entirety of the Aqua plan. Position `oxtrans` as *"future-proofing plus a testing superpower"*, never as *"OpenOSX runs Mac apps"*, or T4 reads as failure when it is the expected handoff.
6. **The clean-room perimeters differ and must not be confused.** XNU, dyld, objc4, libSystem and cctools are APSL-published, so the `is_cambria` bit, `MAP_TRANSLATED_ALLOW_EXECUTE`, `proc_is_translated` and `cpu_x86_64_capabilities.h` are *licensed use of a published API surface*, not reverse engineering. The `AQUA_UI_PLAN.md` Â§9 tier system applies to the never-published frameworks only. For translation the equivalent red line is narrow and absolute: reading dougallj and FFRI's Project Champollion is fine (published research on observable behaviour); extracting, shipping, or deriving code from `/usr/libexec/rosetta/*` or `/var/db/oah/*` is not. Keep a written record that no Apple binary was disassembled *into* the implementation.

---

## 5. Risk register

| Risk | Likelihood | Effort / impact | Mitigation |
|---|---|---|---|
| **A1 stalls: `PDArmPCI` never enumerates AHCI** | Medium | Weeks. Blocks all of Part A | Fix the hardcoded ECAM/MMIO window by reading `reg`/`ranges` from DT. Fallback: write `IOVirtIOBlock` (~500 lines, benefits x86 too) |
| **TCG iteration speed makes arm64 debugging intolerable** | High | Ongoing drag | A2 step 2: ship `.img` to the Mac, run under HVF. Highest-leverage QoL item in Part A |
| **arm64 CI never lands, tree bit-rots again** | High (it already happened upstream) | Silent, expensive | Cheap `nix build .#kernel-arm64-virt .#kexts-arm64 .#kc-arm64` on every push, from A2 |
| **`config_embedded` divergence bites under translation** | Medium | Weeks, late and confusing | Decide deliberately in A2, before T0 |
| **dynarmic's A64 decoder lacks v8.1â€“v8.5 coverage** | **High â€” assume it** | 1â€“3 months of mechanical work | Table-driven decoder + Capstone cross-check + exhaustive coverage tests. Work we would do from scratch anyway |
| **Signal / `mcontext` translation** | High | 1â€“2 months, recurring bug source | Budget it explicitly; it is the known DBT tarpit |
| **Apple ABI divergences (stack-passed variadics, unwidened narrow args, `long double`)** | High | Weeks each, silent corruption if missed | Differential testing on the Mac (Â§A.4 handoff). `long double` has no correct answer â€” document the lossy choice |
| **Address-space collision at `0x100000000`** | Certain if ignored | Cheap now, brutal later | Check on day one of T1 |
| **T2 (translating real dyld) proves intractable** | Medium | Could add months | Loader behind an interface from T1; Option-2 mini-loader stays as fallback |
| **Translated ObjC too slow to be usable** | Medium | Perf ceiling, not correctness | Inline branch caches â†’ return-address stack â†’ native fast-path overrides, in that order |
| **arm64-only GUI apps blocked on AQUA regardless** | Certain | Years | Expectation management (Â§4.5). Not a translation risk â€” a scope fact |
| **Someone tries to build a cross-arch bridging dylib** | Low but catastrophic | Unbounded | Documented hard invariant (Â§B.4): one process, one architecture. Enforced by the object model, not by policy |
| **GPL contamination via QEMU/Unicorn/Asahi drivers** | Low | License-fatal for the image | dynarmic 0BSD / FEXCore MIT only; QEMU strictly an out-of-tree oracle; Asahi read for docs, never ported |

**What may never be worth it, stated plainly:**

- **Apple Silicon bare metal.** ~15 missing driver families, no permissively licensed reference to port from, and a boot chain that requires shipping Apple's own iBoot2 on disk. **VMAPPLE captures most of the value for a small fraction of the cost.** Revisit only if Asahi-adjacent contributors appear.
- **arm64e translation.** Emulating PAC instructions is easy; the arm64e ABI is a large subsystem for zero third-party compatibility gain. Reject the slice.
- **x86_64-on-arm64 as a priority.** If OpenOSX runs on ARM hardware, modern Mac apps there are already arm64 and need no translation. This direction serves only *legacy Intel-only* apps â€” a shrinking set â€” on a host configuration we do not yet have. Building it is correct eventually; prioritising it is not.
- **Metal, under translation or otherwise.** Already parked in `AQUA_UI_PLAN.md`; translation does not change that calculus by one day.
- **Making the Mac a Nix *build* node.** Un-gating `isDarwin` across `nix/arm64.nix`, `flake.nix` and `image.nix` is real work with no payoff while WSL builds fine and the Mac's value is HVF testing. Defer until VMAPPLE.

**Calendar, honestly.** Part A: A0 weeks, A1 the real milestone at 2â€“6 weeks, A2 4â€“10 weeks, A3 3â€“9 months opportunistic â€” so **arm64 to an SSH-able CLI system inside a quarter is realistic**, and that is a genuinely strong result. Part B: T0 2â€“4 weeks, T1 1â€“3 months, T2 2â€“5 months, T3 3â€“6 months, T4 6â€“18 months â€” **roughly 1â€“2 years to transparent, usable arm64-on-x86_64 translation of CLI and Foundation workloads**, with GUI apps gated on a separate multi-year effort. Neither number is comfortable. Both are smaller than they would be for anyone else, for the two reasons in the first paragraph.

---

*Engineering analysis, not legal advice. The clean-room record-keeping in Â§4.6 should be reviewed alongside `docs/AQUA_UI_PLAN.md` Â§9 before any public release that ships `oxtrans`.*
