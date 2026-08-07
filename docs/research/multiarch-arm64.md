# RESEARCH TASK A â€” arm64 OpenOSX: from "targets exist" to "it boots and is usable"

All claims below are grounded in the actual tree at `/root/openosx` (WSL; reachable from Windows as `\\wsl.localhost\Ubuntu\root\openosx`) on branch `openosx-next`, plus the vendored loader at `C:\Users\poopy\OneDrive\Documents\GitHub\OpenOSX\tools\xnu-loader` (identical to flake input `PureDarwin/xnu-loader` rev `58197b99`, locked ~2026-08-04). I also ran a real `nix eval` and `nix build --dry-run` to verify the arm64 image derivation resolves.

---

## 0. Executive summary

The arm64 port is **far more real than "targets exist."** There is a genuine XNU `ARM64_BOARD_CONFIG_VIRT` board config with PL011/GICv3/generic-timer support in pexpert, a real AArch64 UEFI loader that builds Apple-style boot_args + a flattened Apple device tree and `eret`s into EL1, two ARM platform kexts, a full arm64 cross-compiled userland (libSystem, dyld, objc4, CoreFoundation, Foundation, launchd, zsh, toybox, X11, GTK3, Mesa), and four image targets.

But **the tree as committed cannot boot arm64 to a shell**, for four concrete, fixable reasons that I found by reading it:

| # | Defect | Consequence |
|---|---|---|
| **D1** | `arm64-virt-runner` attaches the root disk as `-device virtio-blk-pci` (`/root/openosx/nix/image-contents.nix:970`) but **there is no VirtIO block driver anywhere in the tree** (`src/Kernel/Extensions/IOVirtIOFamily/` contains only `IOVirtIOTransport`; only `IOVirtIONet` and `IOVirtIOGPU` exist) | UEFI reads the ESP fine and XNU boots, then **no `IOMedia` ever appears** â†’ root never mounts â†’ hang/panic waiting for root. This alone is fatal. |
| **D2** | `image-arm64-virt` (`nix/image-contents.nix:657-665`) passes `baseSystem = splitBaseSystem` and `extraPackages = imageExtraPackages` â€” **the x86_64 base system and x86_64 userland** â€” with an arm64 KC | The "default" arm64 image is an arm64 kernel with an x86_64 `/sbin/launchd`. Guaranteed non-boot. Worse: `runArm64Virt` auto-detects **only** `openosx-arm64-virt.img` (`image-contents.nix:937-940`), i.e. exactly the broken image. |
| **D3** | `kc-arm64.nix` links `IOVirtIOGPU.kext`, whose `OSBundleLibraries` requires `com.apple.iokit.IOGraphicsFamily` (`src/Kernel/Extensions/IOVirtIOGPU/Info.plist`), but `kextsArm64Build` sets `enableIOGraphicsFamily = false` (`nix/arm64.nix:1179`) and IOGraphicsFamily is in neither the arm64 kext list nor the arm64 KC | Unresolved KC dependency; and **arm64 has no framebuffer/console path at all** â€” serial only. `image-arm64-virt-full` ships X11+XFCE with no display driver. |
| **D4** | `nix/pkgs/toolchain/kc-arm64.nix` hardcodes its own kext list instead of sharing `nix/lib/kc-kexts.nix` | Exactly the drift that file's own header comment warns about; arm64 and x86 KC contents will silently diverge forever. |

Fix D1 + D2 and there is a credible path to an arm64 serial-console shell in weeks, not months, because everything else is genuinely in place.

---

## 1. Current state â€” what the arm64 targets actually build

### 1.1 Target machine: QEMU `virt` only (plus a vestigial BCM2837/raspi3 path)

`src/Kernel/xnu/pexpert/pexpert/arm64/QEMUVIRT.h` is a real board header matched against a real QEMU virt DTB:

```
QEMUVIRT_UART_BASE_PHYS   0x09000000   (PL011)   IRQ 33 (SPI 1)
QEMUVIRT_GICD_BASE_PHYS   0x08000000   size 0x10000
QEMUVIRT_GICR_BASE_PHYS   0x080a0000   size 0xf60000   (GICv3 redistributors)
QEMUVIRT_RAM_BASE_PHYS    0x40000000
Timer PPIs: sec 29, nonsec 30, virt 27, hyp 26
__ARM_16K_PG__ 1, __ARM_VMSA__ 8, NO_MONITOR, NO_ECORE, ARM_ARCH_TIMER
PLATFORM_PANIC_LOG_PADDR 0x47000000
```

Wired in at `pexpert/pexpert/arm64/board_config.h:233-241` under `ARM64_BOARD_CONFIG_VIRT` (`MAX_CPUS 4`, `MAX_CPU_CLUSTERS 1`). Real code paths exist in `pexpert/arm/pe_serial.c` (lines 722, 855-859 â€” PL011 TX) and `pexpert/arm/pe_identify_machine.c` (lines 57, 537, 636).

Selected by `-DOPENOSX_ARM64_MACHINE_CONFIG=VIRT` (`nix/arm64.nix:1130,1142`) â†’ `MACHINE_CONFIGS=VIRT` in `src/Kernel/xnu/CMakeLists.txt:17-26`. **The default if unset is `BCM2837`** (raspi3), which is why there is also an `arm64-uboot-runner`. That path is stale â€” treat VIRT as the only live target.

Config file: `src/Kernel/xnu/config/MASTER.arm64.virt`. Note its `KERNEL_BASE` is `[ arm64 xsmall msgb_small config_embedded config_requires_u32_munging ... ]` â€” i.e. an **embedded-flavored** kernel with `CONFIG_VNODES=1024`. Contrast `MASTER.x86_64:19` â†’ `[ intel medium msgb_large ... ]`. Notably `MASTER.arm64.virt` *drops* `config_enforce_signed_code` and `config_darkboot` that plain `MASTER.arm64` has â€” deliberate and correct for an unsigned kernel. But `config_embedded` remaining on means jetsam/memorystatus, task limits, and several BSD paths behave differently from x86 under the same userland. Flag this as a semantic divergence to revisit (see A2 below).

**Apple Silicon is not targeted.** `ARM64_BOARD_CONFIG_T8103` (M1) and `ARM64_BOARD_CONFIG_VMAPPLE` blocks *do exist* in `board_config.h:208-218` and `243-254` â€” inherited from the XNU 20.5 source drop â€” but nothing in the build selects them, and there are no Apple-SoC kexts (no AIC, no DART/SART, no ANS2 NVMe, no Apple Display Pipe).

### 1.2 Kernel targets

| Attr | pname | Config | Machine |
|---|---|---|---|
| `kernelArm64Build` | `openosx-kernel-arm64` | RELEASE | **BCM2837** (default; no `MACHINE_CONFIG` flag) â€” effectively dead |
| `kernelArm64VirtBuild` | `openosx-kernel-arm64-virt` | RELEASE | VIRT |
| `kernelArm64VirtDebugBuild` | `openosx-kernel-arm64-virt-debug` | DEBUG | VIRT |

(`nix/arm64.nix:1109-1143`)

### 1.3 arm64 kexts

`kextsArm64Build` (`nix/arm64.nix:1144-1182`) builds 29 kexts. ARM-specific ones:

- **`PDArmPlatformExpert`** (`src/Kernel/Extensions/PDArmPlatformExpert/`) â€” `IODTPlatformExpert` subclass. Matches `IONameMatch = "ACPI"` on `IOPlatformExpertDevice` (the loader sets root `compatible`/`model` = `"ACPI"`, see 1.4). `start()` calls `PDArmGIC_init()` then creates **exactly one** `PDArmCPU`. Reports machine `"arm64"`, model `"QEMU Virtual ARM64"`.
- **`PDArmPCI`** (`src/Kernel/Extensions/PDArmPCI/PDArmPCI.cpp`) â€” `IOPCIBridge` over ECAM. **Hardcodes** `kECAMBase = 0x4010000000` (QEMU virt *highmem* ECAM) and bridge MMIO window `0x10000000 + 0x30000000`. Matches `IONameMatch = "pci"` on `IOPlatformDevice`. Has a nice comment documenting a real bug they already fixed (byte-offset must not be dword-masked or header-type reads are wrong).

Everything else is arch-neutral and shared with x86: `IOPCIFamily`, `IOStorageFamily`, `RavynAHCIPort`, `IONVMEFamily`, `IOVirtIOFamily/Net/GPU`, `IONetworkingFamily`, `IOHIDFamily`, full USB stack (EHCI/OHCI/xHCI/HID/composite), filesystems (`hfs`, `apfs`, `ext4`, `msdosfs`, `HFSEncodings`), `corecrypto`, `pthread`, `PDE1000`, `RavynHDAudio`.

**Explicitly absent on arm64:** `IOACPIFamily`, `AppleAPIC`, `AppleI386*`, `IOGraphicsFamily`, `IOGOPFramebuffer`, `IOIntelGen9Framebuffer`, `IOATAFamily`. The first four are correct (x86-only); the framebuffer omission is defect **D3**.

Boot KC assembled by `nix/pkgs/toolchain/kc-arm64.nix` via PureDarwin's `kc-tools/kc-builder`: 29 `-kext` entries + codeless `System.kext` plugins, output `kernel`.

### 1.4 Boot path: AAVMF â†’ `BOOTAA64.EFI` (xnu-loader) â†’ XNU at EL1

`xnuLoader = xnu-loader.packages.${system}.arm64-virt` (`image-contents.nix:661,670,685,699`) â†’ `pkgsCross.aarch64-multiplatform` build with `-DXNU_LOADER_QEMU_VIRT=ON`, installed to `img/EFI/BOOT/BOOTAA64.EFI` (`tools/xnu-loader/default.nix:18`).

The AArch64 path in the loader is real work, not a stub:

- **`src/jump.S:1-86`** â€” handoff derived from `osfmk/arm64/start.s`: `x0 = phys(boot_args)`, branch to `_start` **at EL1**. If it finds itself at EL2 (u-boot case) it sets `HCR_EL2.RW=1`, clears `SCTLR_EL2.M/C/I`, zeroes `SCTLR_EL1`, and `eret`s to EL1h. The comment documents the actual bug they hit (ESR_EL2 translation fault) that led to this.
- **`src/boot.c:860-935`** â€” `arm64_boot_build_args()` builds the ARM `boot_args` (Revision/Version, virtBase/physBase/memSize/topOfKernelData, deviceTreeP/Length, CommandLine) at fixed `XNU_ARM64_BOOTARGS_PHYS`.
- **`src/main.c:56-86, 225-255`** â€” arm64-specific staging allocator that walks candidate L2-block-aligned slots from `0x40000000`, plus a trustcache page below the kernel and a boot-info block after it.
- **`src/devtree.c:1012-1071, 1218-1328`** â€” arm64-only DT properties XNU actually requires: `dram-base`/`dram-size` (arm_init panics without them), an **empty static TrustCache** under `/chosen/memory-map`, a `/defaults` node (pmap_bootstrap does an unconditional `SecureDTLookupEntry("/defaults")`), `/cpus/cpu@0` with `timebase-frequency` read from `CNTFRQ_EL0`, `/arm-io` (`device_type = "qemuvirt-io"`) with `uart0` at `0x01000000` under `ranges { 0, 0x08000000, 0x08000000 }`, and `/pci` (`compatible = "pci-host-ecam-generic"`, `bus-range 0..255`). Root node gets `compatible`/`model` = `"ACPI"` so the platform-expert match works.
- 16K paging and a raised Mach-O max-segment limit (32â†’256; arm64 filesets need â‰¥110) are in upstream loader commits from July 2026.

`image.nix` writes `BOOTAA64.EFI` + the KC as `kernel` + `boot-args.txt` to the ESP (`image.nix:91-93`, `efiBinary` param at line 29).

**Gap:** the loader's `/pci` node carries no `reg`/`ranges`, and `PDArmPCI` doesn't read them anyway â€” both sides hardcode. Fine for stock `-M virt`; breaks under `highmem=off`, older machine revisions, or any non-QEMU platform.

### 1.5 The four arm64 images

| Target | baseSystem | extraPackages | KC | esp/root MB | bootArgs |
|---|---|---|---|---|---|
| `image-arm64-virt` | âŒ **x86 `splitBaseSystem`** | âŒ **x86** | arm64 DEBUG | 64/4096 | default (x86-flavored) |
| `image-arm64-virt-minimal` | `â€¦Arm64VirtMinimal` | *(none)* | arm64 **DEBUG** | 64/256 | `debug=0x219 -nogzalloc_mode keepsyms=1 serial=3 gopconsole=1 -noprogress gen9_debug=1 vgpu_debug=1 pdtrace=1 ahci_debug=1 no_interrupt_masked_debug=1` |
| `image-arm64-virt-minimal-release` | `â€¦Arm64VirtMinimalRelease` | zsh, libiconv, toybox | arm64 RELEASE | 64/512 | `serial=3 -noprogress ahci_debug=1 kext=0xffff io=0xffff` |
| `image-arm64-virt-full` | `â€¦Arm64VirtMinimalRelease` | full `imageExtraPackagesArm64` (~130 pkgs) | arm64 RELEASE | 64/3072 | `serial=3 -noprogress` |

(`nix/image-contents.nix:657-706, 1005-1008`)

**minimal** contents (`nix/arm64.nix:1188-1256`): arm64 libSystem (+dyld), icucore, libc++abi, libc++, libobjc, CoreFoundation, IOKit, launchd (relocated `pd-sbin/launchd` â†’ `/sbin/launchd`), launchctl, the `userland` CLI set, kernel, kexts. **-minimal-release** is identical except RELEASE kernel.

**full** adds the entire arm64 twin of the x86 desktop stack: Xvfb/Xorg, i3, xterm, GTK3, Pango/Cairo/HarfBuzz/FreeType/fontconfig, Mesa + demos + virgl shim, NetSurf/Dillo, Python, git, OpenSSH, OpenSSL, curl, Foundation, Security, SystemConfiguration, OpenGL.framework, fastfetch (`nix/arm64.nix:1262-1371`). Note XFCE is **not** in the arm64 set (`nix/xfce.nix` is x86-only) â€” arm64 "full" means i3, not XFCE.

### 1.6 Runner apps

`nix run .#arm64-virt` â†’ `openosx-arm64-virt` (`image-contents.nix:924-979, 1038-1041`):

```
qemu-system-aarch64 -machine virt,gic-version=3 -boot order=c,strict=on
  -cpu ${OPENOSX_ARM64_VM_CPU:-max} -smp ${OPENOSX_VM_SMP:-4} -m ${OPENOSX_VM_MEMORY:-4096}
  -drive if=pflash,unit=0,readonly=on,file=AAVMF_CODE.fd
  -drive if=pflash,unit=1,file=$state/AAVMF_VARS.fd
  -drive if=none,id=system,file=$image,format=raw[,snapshot=on]
  -device virtio-blk-pci,drive=system,bootindex=1      # â† D1
  -device virtio-net-pci,netdev=net0 -netdev user,id=net0,hostfwd=tcp::2223-:22
  -serial mon:stdio -display none -no-reboot -no-shutdown
```

Also `nix run .#arm64-uefi` (firmware-only smoke test, adds `virtio-gpu-pci`, gtk display) and `nix run .#arm64-uboot` (BCM2837-era path, **uses `ich9-ahci` + `ide-hd`** â€” which is the topology that would actually work).

**Critical scoping fact:** `arm64Packages`, `imageExtraPackageSetArm64`, `linuxPackages`, `linuxApps` and even `mkArm64Build` itself are all `isDarwin`-gated. `nix/arm64.nix:978-979` reads literally `mkArm64Build = file: deps: if isDarwin then null else â€¦`, and `flake.nix:3306-3307` is `packages = commonPackages // arm64Packages // probePackages // lib.optionalAttrs (!isDarwin) linuxPackages; apps = lib.optionalAttrs (!isDarwin) linuxApps;`. **On `aarch64-darwin` or `x86_64-darwin` there are no arm64 packages, no image targets, and no apps at all.** This is decisive for Â§4/Â§5.

---

## 2. Known-working vs known-incomplete upstream

**PureDarwin PR #171 "Merge arm64 into next"** (merged by maintainer Vali0004, self-merged): describes "a full qemu-virt arm64 target," "arm64 kernel/loader support" with segment reordering, fileset support, and QEMUVIRT pexpert config, plus IOVirtIOFamily kexts. Explicitly scoped to **QEMU virtualization, not Apple Silicon**. It also removed `docs/STUBS_AND_PATCHES.md`, `docs/XNU_PATCHES.md` and the 10.5k-line `docs/xnu-7195.121.3.diff` â€” so the previously-documented XNU patch inventory is gone; the maintainer's note is that "~98% of XNU kernel changes are documented and necessary."

**`PureDarwin/xnu-loader` commit history** (the input this tree pins) shows arm64 as an *active, recent* workstream:

- 2026-07-19 â€” port to arm64 + AArch64-host build restructure
- 2026-07-20 â€” **"arm64: QEMU Virt, improve devtree to actually boot"**
- 2026-07-29 â€” "arm64: Fix various arm64 issues, including changing to 16K paging"
- 2026-07-29 â€” "misc/arm64: Raise macho max segments to 256 from 32" (arm64 needs â‰¥110)
- 2026-08-04/05 â€” ACPI config-table exposure fix; `FB_TEXT_MODE` on `-v`

**`PureDarwin/PureDarwin` `next` branch, Julâ€“Aug 2026** â€” the visible commit stream is *all x86*: USB/MSD/EHCI + the ext4-locking launchd fix, Intel Gemini Lake DPâ†’HDMI, Mesa/glxgears over softpipe, Wine, WebKitGTK, new libc++/libc++abi. **No arm64 commits after the #171 merge.**

**Issue #137 "Tracking issue for boot loader"** (opened 2024-01-08, now closed) framed ARM64 as "if possible" â€” a secondary goal.

**Honest read:** upstream arm64 is a *bring-up branch that was merged, then left behind* while the maintainer drove x86 to a working desktop. The loader-side arm64 work is fresher and better maintained than the OS-side arm64 work. Nobody has publicly claimed "arm64 boots to a shell." The defects I found (D1â€“D4) are exactly the shape of "merged after the last successful manual test, then bit-rotted" â€” in particular D2 (x86 base system in `image-arm64-virt`) looks like a copy-paste that was never re-run.

Prior art worth knowing about for later phases: [alephsecurity/xnu-qemu-arm64](https://github.com/alephsecurity/xnu-qemu-arm64) (iOS XNU on QEMU, reached launchd + interactive shell) and [Booting a macOS Apple Silicon kernel in QEMU](https://worthdoingbadly.com/xnuqemu3/) (booted an M1 XNU to launchd) â€” both are strong references for the debugging techniques when the kernel gets stuck.

---

## 3. How to build and test arm64 from x86_64 Linux/WSL â€” exact commands

### 3.1 Verified state of your WSL host

- 12 threads, 61 GB RAM, 913 GB free on `/` â€” plenty (much better than the ~22 GB in my memory notes).
- Determinate Nix 3.21.9 / 2.34.8 at `/root/.nix-profile/bin/nix`.
- `MacOSX11.3.sdk.tar.xz` already registered: `/nix/store/blj9dgi4hnxr21jl19hgqb4zsdyx51zz-MacOSX11.3.sdk.tar.xz`. âœ…
- `qemu-system-aarch64` is **not** in `$PATH` â€” irrelevant, the runner brings `pkgs.qemu` via `runtimeInputs`.

### 3.2 The build actually resolves â€” I ran it

```
$ nix eval --raw .#packages.x86_64-linux.image-arm64-virt-minimal-release.drvPath
/nix/store/k3v6r1qhbjk9k2rf4vym1c89ffps972h-openosx-image-0.1.drv

$ nix build --dry-run .#image-arm64-virt-minimal-release
these 36 derivations will be built ... these 26 paths will be fetched (100.2 MiB)
```

Only **36 derivations** remain, because the x86 build already cached the shared host tooling. The list is exactly what you'd want to see:

```
arm64-apple-darwin20.4-{clang,clang++,ld,ar,ranlib,nm,strip,objdump,otool,lipo,
                         install_name_tool,dsymutil}, xcrun, darwin-cross-toolchain-nix
openosx-libsystem, -icucore, -libcxxabi-dylib, -libcxx-dylib, -libobjc,
openosx-corefoundation, -iokit, -libiconv, -launchd, -launchctl, -userland,
openosx-zlib, -ncurses, -zsh, -toybox
openosx-kernel-arm64-virt, openosx-kexts-arm64, openosx-kc-arm64
gnu-efi-aarch64-unknown-linux-gnu, xnu-loader-aarch64-unknown-linux-gnu
openosx-basesystem-arm64-virt-minimal-release, openosx-image
```

Rough wall-clock estimate on 12 threads: **1.5â€“3 h** (the arm64 XNU build and the 29-kext build dominate).

### 3.3 Build

```bash
cd /root/openosx

# START HERE. This is the only coherent, small, RELEASE-kernel arm64 image.
nix build -L .#image-arm64-virt-minimal-release
ls -l result/openosx-arm64-virt-minimal-release.img

# Debug kernel (verbose, slower) once you need kernel-side logging:
nix build -L .#image-arm64-virt-minimal

# Full arm64 userland (X11/i3/Mesa/NetSurf) â€” only after minimal boots:
nix build -L .#image-arm64-virt-full

# DO NOT build .#image-arm64-virt â€” it is the broken x86-userland mix (D2).

# Useful sub-targets for bisecting a failure:
nix build -L .#kernel-arm64-virt          # kernel alone
nix build -L .#kexts-arm64                # kext bundles alone
nix build -L .#kc-arm64                   # boot kernel collection alone
```

### 3.4 Run headless â€” **do not use the runner as-committed**

The stock runner will not mount root (D1). Run QEMU directly with an AHCI disk, matching `RavynAHCIPort` which *is* in the arm64 KC:

```bash
cd /root/openosx
IMG=$PWD/result/openosx-arm64-virt-minimal-release.img
STATE=$PWD/.openosx-arm64-virt; mkdir -p "$STATE"

# One-time: get AAVMF + qemu into the shell
nix shell nixpkgs#qemu nixpkgs#pkgsCross.aarch64-multiplatform.OVMF.fd

AAVMF=$(dirname $(readlink -f $(command -v qemu-system-aarch64)))/../share  # or use the nix store path
CODE=/nix/store/<...>-OVMF-*/FV/AAVMF_CODE.fd
VARS=$STATE/AAVMF_VARS.fd
[ -e "$VARS" ] || { cp /nix/store/<...>/FV/AAVMF_VARS.fd "$VARS"; chmod u+w "$VARS"; }

qemu-system-aarch64 \
  -machine virt,gic-version=3 \
  -cpu max,pauth-impdef=on,sve=off \
  -smp 1 \
  -m 4096 \
  -boot order=c,strict=on \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
  -drive if=pflash,format=raw,unit=1,file="$VARS" \
  -drive if=none,id=system,file="$IMG",format=raw,snapshot=on \
  -device ich9-ahci,id=ahci0 \
  -device ide-hd,drive=system,bus=ahci0.0 \
  -device virtio-net-pci,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2223-:22 \
  -serial mon:stdio \
  -display none -no-reboot -no-shutdown
```

Rationale for each deviation from the committed runner:

- **`ich9-ahci` + `ide-hd` instead of `virtio-blk-pci`** â€” the only block driver in the arm64 KC is `RavynAHCIPort`. The `arm64-uboot-runner` already does this (`image-contents.nix:915-916`); the virt runner should too.
- **`-smp 1`** â€” `PDArmPlatformExpert::start()` creates exactly one `PDArmCPU`, and the loader's DT contains only `/cpus/cpu@0` (`devtree.c:1230-1243`). There is no PSCI secondary bring-up. `-smp 4` just gives you three CPUs XNU never starts. (`board_config.h` caps `MAX_CPUS 4` regardless.)
- **`-cpu max`** is *mandatory*, not optional: `QEMUVIRT.h` sets `__ARM_16K_PG__ 1`, and 16 KB granule (`ID_AA64MMFR0_EL1.TGran16`) is not implemented by `cortex-a53/a57/a72`. Only `max` (and modern Neoverse models) advertise it.
- **`pauth-impdef=on, sve=off`** â€” pure TCG speed. QEMU's architected QARMA PAuth emulation is extremely slow; the impdef variant is a cheap hash. XNU arm64 (non-`arm64e`) doesn't use PAC, but `-cpu max` advertises it and TCG pays the cost on any incidental use. SVE similarly costs translation-time for nothing.

### 3.5 Automate it (adapt the existing harness)

`/root/openosx/tools/testing/boot-test.sh` is x86-only (`qemu-system-x86_64`, OVMF, 420 s timeout). Clone it to `boot-test-arm64.sh` with the command above, and **raise the timeout to â‰¥1800 s** â€” see Â§4. Keep the same verdict logic (`PASS` on regex, `PANIC` on `Kernel panic|panic\(cpu`, `TIMEOUT`), and use a staged pass regex:

| Stage | Pass regex |
|---|---|
| Loader reached | `arm64 boot_args @` |
| Kernel entered | `Darwin Kernel Version` |
| IOKit platform up | `OpenOSX PDArmPCI: probe` |
| Root found | `BSD root:` |
| PID 1 | the OpenOSX launchd banner |

Those `IOLog`s already exist in `PDArmPCI.cpp:15,18,25,43` â€” free instrumentation.

### 3.6 If you want the runner fixed properly (recommended, ~10 lines)

In `/root/openosx/nix/image-contents.nix`:

1. `runArm64Virt` (line ~970): replace `-device virtio-blk-pci,drive=system,bootindex=1` with `-device ich9-ahci,id=ahci0` + `-device ide-hd,drive=system,bus=ahci0.0,bootindex=1`; change `OPENOSX_VM_SMP` default 4 â†’ 1; change `OPENOSX_ARM64_VM_CPU` default `max` â†’ `max,pauth-impdef=on,sve=off`.
2. `runArm64Virt` image auto-detect (lines 937-940): add `openosx-arm64-virt-minimal-release.img` and `openosx-arm64-virt-full.img` ahead of `openosx-arm64-virt.img`.
3. `imageArm64VirtBuild` (lines 657-665): either fix it to use `splitBaseSystemArm64VirtMinimalRelease` + `imageExtraPackagesArm64`, or **delete the target** so nobody builds it by accident.

---

## 4. Performance expectations

### 4.1 QEMU TCG aarch64-on-x86_64, Ryzen 5600X (6C/12T, ~4.6 GHz boost)

There is no KVM path here â€” cross-ISA means pure TCG. MTTCG (multi-threaded TCG) is supported for aarch64 guests on x86 hosts, but you're pinning `-smp 1` anyway (Â§3.4), so you get **one host thread doing all guest execution**. Expect:

- **~10â€“30Ã— slower than native ARM** of comparable class for ordinary integer/memory code. Cross-ISA TCG typically lands 50â€“400 MIPS/vCPU on a modern desktop core.
- **Worse than that for kernel bring-up specifically**, because early boot is dominated by (a) MMIO â€” every PL011 character is a TCG exit, (b) TLB/MMU manipulation, and (c) 16 KB-granule page-table walks that QEMU's softmmu handles on a colder path than 4 KB.
- **Verbose serial is the single biggest cost.** `image-arm64-virt-minimal` ships `debug=0x219 â€¦ kext=0xffff io=0xffff`-class logging on a DEBUG kernel; every log line is a byte-at-a-time PL011 poll loop.

Concrete estimates (treat as order-of-magnitude, not measurements):

| Scenario | Boot â†’ login/shell |
|---|---|
| `image-arm64-virt-minimal-release`, quiet args, `-smp 1` | **~2â€“6 min** |
| `image-arm64-virt-minimal` (DEBUG kernel, full verbose) | **~10â€“25 min** |
| `image-arm64-virt-full` (X11/i3/Mesa via llvmpipe/softpipe) | boot ~5â€“10 min; **desktop interaction will be miserable** â€” softpipe under TCG is roughly 100â€“1000Ã— off native |

For comparison, your x86 path today runs `qemu-system-x86_64 -accel tcg` in `boot-test.sh` but *can* use `.#kvm` â€” WSL2 on Win11 with AMD nested virt gives near-native for x86. arm64 has no such escape on this host.

**Practical implications:** budget â‰¥1800 s boot-test timeouts; do all interactive iteration on the release+quiet image; keep `-m 4096` (RAM is free and swapping under TCG is catastrophic); consider `-serial file:` rather than `mon:stdio` for CI so terminal I/O isn't in the loop.

### 4.2 Native on an Apple Silicon Mac with HVF

This is a **step-function improvement, not an increment**:

- `qemu-system-aarch64 -M virt,gic-version=3 -accel hvf -cpu host` runs guest EL1/EL0 **natively on the M-series core**. Boot times drop to **seconds**; the whole `boot â†’ shell` loop becomes as fast as your x86+KVM loop.
- HVF on Apple Silicon only supports **GICv3** â€” the flake already uses `gic-version=3`. âœ…
- Guest 16 KB granule: Apple cores are 16 KB-native, so this is the *good* case, not a risk.
- `jump.S` already handles both the EL2-entry and EL1-entry cases, and AAVMF under HVF starts the payload at EL1 â€” the existing handoff should be correct unmodified.
- Firmware on macOS: use QEMU's bundled `share/qemu/edk2-aarch64-code.fd` + `edk2-arm-vars.fd` (Homebrew `qemu`), not the nixpkgs `pkgsCross` AAVMF. [UTM](https://mac.getutm.app/) is a reasonable GUI frontend over the same HVF backend.

**But: you cannot build the image on the Mac today.** `mkArm64Build` returns `null` when `isDarwin` (`nix/arm64.nix:978-979`), and every image target lives in the `isDarwin`-gated `linuxPackages` (`flake.nix:3306`). Also `image.nix` has `meta.platforms = platforms.linux` and needs `sgdisk`/`mkfs.ext4`/`mtools`/`apfsprogs`.

**Therefore the correct Mac play is: build the `.img` in WSL, `scp` it to the Mac, run it under HVF there.** That is available *immediately* and is by far the highest-leverage thing you can do for arm64 iteration speed. It also fits task #14 ("Wire Mac as SSH build/test node") â€” start with the Mac as a *test* node only, and defer making it a *build* node.

---

## 5. Apple Silicon bare metal â€” honest assessment

**Short version: this is a multi-year, multi-person project that is out of scope for OpenOSX in its current form. Do not put it on the roadmap as a deliverable.**

### 5.1 What has to happen just to get code running

XNU on Apple Silicon is launched by **iBoot**, which only executes an Image4-wrapped, signed payload. The community route (Asahi's) is:

1. Set the machine to **Permissive Security** for a specific OS install (1TR + `bputil`).
2. Create a "**stub macOS**" APFS container (~2.5 GB) per third-party OS containing iBoot2, the machine's firmware blobs, an XNU-slot kernel, and RecoveryOS â€” Asahi's docs state a **1:1 mapping between an installed OS and an OS as seen by the platform**, so *Apple's own boot artifacts must be present on disk alongside your OS*.
3. Your kernel goes in the XNU slot; Apple's tooling wraps and installs it. [m1n1](https://github.com/AsahiLinux/m1n1) occupies that slot and describes itself as "a bridge between the XNU boot protocol and the Device Tree / ARM64 Linux boot protocol."

For OpenOSX this is *ironic but real*: **booting an all-open Darwin on Apple Silicon requires shipping Apple's iBoot2 + firmware in a stub container.** That collides head-on with the project's "no closed Apple binaries anywhere in the boot chain" rule in `README.md`. You'd have to either accept the compromise loudly, or scope Apple Silicon as "user installs it themselves from their own machine's firmware" (which is what Asahi does).

### 5.2 What has to happen to get anything *useful*

Asahi's m1n1 stage-1 alone must: parse Apple Device Trees, bring up memory controllers, USB-C/tps6598x, displays, **and initialize NVMe** before it can even mount the ESP and chainload stage 2. m1n1 also does GPU initialization specifically so downstream kernels don't have to handle Apple's float-encoded init tables.

Then OpenOSX would need, from scratch, kernel drivers for:

- **AIC** (Apple Interrupt Controller) â€” not GIC. `PDArmGIC.cpp` is useless here.
- **DART / SART** IOMMUs â€” nearly every DMA-capable Apple block sits behind one.
- **ANS2 NVMe** â€” Apple's NVMe is not spec-compliant; it's a coprocessor with a mailbox (RTKit) protocol. `IONVMEFamily` does not apply.
- **RTKit** â€” the shared coprocessor IPC used by ANS2, SMC, display, GPU, SEPâ€¦
- **Apple Display Pipe / DCP** â€” display is driven by another RTKit coprocessor.
- **Apple SoC clock/power (PMGR)**, SMC, thermals, CPU idle/frequency.
- USB (dwc3 + Type-C PD), SPI/IÂ²C HID for the internal keyboard/trackpad, WiFi (brcmfmac over a bespoke PCIe path), Bluetooth, audio.

Asahi has been at this since late 2020 with a funded team and is still shipping progress reports in 2026 (Linux 7.1, June 2026). Their *advantage* is Linux's mature driver framework and DT bindings; OpenOSX would be writing all of it as IOKit C++ kexts against a 2021-vintage XNU 20.5 KPI, with no reference implementation to port from (Asahi's drivers are GPL â€” **and per your BSD/ISC-only policy in `bsd-driver-porting-strategy.md`, you cannot copy them**). You'd be reimplementing from their *documentation* only.

### 5.3 The two things that *are* worth doing

1. **`ARM64_BOARD_CONFIG_VMAPPLE`** already exists in `board_config.h:243-254` with `MAX_CPUS 32`, `USE_APPLEARMSMP 1`, and there's a `pexpert/arm64/VMAPPLE.h`. This is the board config for a guest under **Apple's Virtualization.framework**. Also `pe_serial.c:722` shares the PL011-ish path between `VMAPPLE_UART` and `QEMUVIRT_UART`. A `VMAPPLE` target would let OpenOSX run as a *native accelerated guest on Apple Silicon* â€” all of the performance benefit of Apple hardware, none of the bare-metal driver problem. **This is by far the best ROI arm64 direction after QEMU virt works**, and it's a genuine differentiator (a Darwin that boots in Apple's own hypervisor).
2. **`ARM64_BOARD_CONFIG_T8103`** exists too, which means the XNU side of M1 support is *nominally* present in-tree. That is a long way from booting, but it means the kernel isn't the blocker â€” the ~15 missing driver families are.

Set expectations accordingly: **QEMU virt â†’ VMAPPLE guest â†’ (maybe, someday, with help) bare metal.**

---

## 6. Phased plan A0 â†’ A3

### A0 â€” Unblock and prove the boot chain *(1â€“2 weeks)*

**Work**
1. Fix **D1**: switch `runArm64Virt` to `ich9-ahci`+`ide-hd`, `-smp 1`, `-cpu max,pauth-impdef=on,sve=off`.
2. Fix **D2**: repair or delete `image-arm64-virt`; make the runner auto-detect `-minimal-release` first.
3. Fix **D3**: either add `IOGraphicsFamily` to `kextsArm64Build`/`kc-arm64.nix`, or drop `IOVirtIOGPU.kext` from the arm64 KC. (Dropping it is the right A0 move â€” you want serial-only anyway.)
4. Fix **D4**: make `kc-arm64.nix` consume `nix/lib/kc-kexts.nix` filtered by an `arch` predicate, so the two KCs can never silently diverge again.
5. Clean the arm64 boot args: drop `gopconsole=1 gen9_debug=1 vgpu_debug=1` (x86 framebuffer flags, meaningless on virt).
6. Write `tools/testing/boot-test-arm64.sh` with staged pass regexes and a 1800 s timeout.

**Success criteria**
- `nix build .#image-arm64-virt-minimal-release` succeeds.
- Serial log shows, in order: `xnu-loader` banner â†’ `DT: built N bytes` â†’ `arm64 boot_args @ 0xâ€¦` â†’ `Darwin Kernel Version 20.5.0` â†’ `OpenOSX PDArmPCI: probe`.
- Even a panic *after* `Darwin Kernel Version` counts as A0 success â€” the boot chain is proven.

### A1 â€” Root device + PID 1 *(2â€“6 weeks)*

**Work**
1. Get `PDArmPCI` to enumerate the AHCI controller and `RavynAHCIPort` to publish `IOMedia`. Watch for the QEMU virt highmem-ECAM assumption and the `addBridgeMemoryRange(0x10000000, 0x30000000)` window vs. what AAVMF actually assigned to the ich9-ahci BAR â€” a BAR outside that window is the most likely first failure.
2. Confirm the loader's `find_ext4_boot_uuid()` path (`devtree.c:705-728`) finds the ext4 root on the arm64 image and that `Ext4FileSystemDriver` publishes `boot-uuid-media`.
3. Reach `BSD root: â€¦` and exec `/sbin/launchd`.
4. Expect to re-fight arm64 versions of x86 bugs already fixed upstream â€” notably the **ext4 locking issue that broke launchd** (fixed on `next` at commit `8f6a7b86`, Aug 2026) and dyld/chained-fixups issues on arm64 Mach-O.
5. Consider making `PDArmPCI` read `reg`/`ranges` from the `/pci` DT node, and teach `devtree.c` to emit them (both sides currently hardcode).

**Success criteria**
- Root mounts; OpenOSX launchd banner on serial; `zsh` prompt on `-serial mon:stdio` (mirror `tools/testing/interactive-test.sh`).
- **This is arm64's M2+M3+M4 equivalent â€” the real milestone.**

### A2 â€” Usable arm64 CLI + Mac-accelerated loop *(4â€“10 weeks)*

**Work**
1. `image-arm64-virt-minimal-release` becomes a genuinely usable CLI system: toybox, zsh, file, curl/OpenSSL, git, python, openssh. Get `virtio-net-pci` + `IOVirtIONet` up so `hostfwd tcp::2223-:22` actually reaches sshd â€” that converts the arm64 VM from "serial toy" to "SSH-able build/test target."
2. **Ship `.img` to the Mac and run under HVF** (`-accel hvf -cpu host`). Document it as the standard arm64 dev loop. This is the single biggest quality-of-life win.
3. Add an arm64 CI job. Today `.github/workflows/build-dev.yml` and `build-pr.yml` contain **zero** arm64 references. At minimum: `nix build .#kernel-arm64-virt .#kexts-arm64 .#kc-arm64` on every push (cheap, catches bit-rot), plus a nightly `image-arm64-virt-minimal-release` + `boot-test-arm64.sh`.
4. Revisit **`config_embedded` in `MASTER.arm64.virt`**. x86 builds `[intel medium msgb_large â€¦]`; arm64 builds `[arm64 xsmall msgb_small config_embedded â€¦]` with `CONFIG_VNODES=1024`. Under the same macOS-flavored userland this is a divergence you will eventually trip over (jetsam/memorystatus, task limits, `posix_spawn` semantics, code-signing paths). Decide deliberately: either move arm64-virt toward the MacOSX flavor, or document why embedded is correct.
5. Add a VirtIO block driver (`IOVirtIOBlock` on the existing `IOVirtIOTransport`). Not needed if AHCI works, but it's ~500 lines, benefits x86 too, and removes the AHCI dependency entirely.

**Success criteria**
- `ssh -p 2223 root@localhost` into an arm64 OpenOSX VM.
- Arm64 boot regression test green in CI.
- Mac HVF loop documented and reproducible.

### A3 â€” Graphics, and the VMAPPLE target *(3â€“9 months, opportunistic)*

**Work**
1. Restore `IOGraphicsFamily` for arm64 and get `IOVirtIOGPU` working on `virtio-gpu-pci`, so `image-arm64-virt-full` has an actual display. Then Xvfb â†’ Xorg â†’ i3. Expect this to be *painfully* slow under TCG and *fine* under HVF on the Mac â€” which is the real argument for A2 step 2 preceding this.
2. **Build a `VMAPPLE` machine config** (`ARM64_BOARD_CONFIG_VMAPPLE` + `pexpert/arm64/VMAPPLE.h` already exist; `pe_serial.c` already shares its UART path with QEMUVIRT). Target Apple's Virtualization.framework. This gives near-native arm64 OpenOSX on the user's own Mac, with the Mac's own accelerated virtio devices â€” the strongest arm64 story available without bare-metal drivers.
3. Un-gate arm64 on `isDarwin` so the Mac can build natively (`nix/arm64.nix:978`, `flake.nix:3306-3307`, `image.nix` `meta.platforms`) â€” worth doing only once VMAPPLE justifies it.
4. Explicitly park bare-metal Apple Silicon. Revisit only if Asahi-adjacent contributors show up.

**Success criteria**
- i3 desktop on arm64 under HVF at usable framerates.
- OpenOSX boots as a Virtualization.framework guest on Apple Silicon.

---

## 7. Relevance to Task B (cross-architecture translation)

Two findings from this tree bear directly on the translator architecture:

1. **The "both slices from one source tree" premise is already true and already wired.** `nix/arm64.nix` produces genuine arm64 builds of libSystem/dyld, libobjc, libc++/libc++abi, ICU, CoreFoundation, Foundation, IOKit, Security, SystemConfiguration â€” from the *same sources* as the x86_64 set, via `mkArm64Build`/`.override` rather than duplicated derivations. That is exactly the Rosetta-2-style "foreign-arch system dylib slice" substrate a translator needs, and it costs ~36 extra derivations, not a second port.
2. **Two divergences will bite the translator and should be closed early.** (a) `MASTER.arm64.virt` is `config_embedded` while x86_64 is the desktop flavor â€” a translator marshalling between the two ABIs on one kernel must not have the *kernel's* personality differ by which slice is running. (b) `clangTarget` is `${arch}-apple-macosx11.0` for both (`nix/lib/target-info.nix:25`), which is right â€” keep it that way; a deployment-target skew between slices would change struct layouts and availability guards.

---

## Key file references

| Path | What |
|---|---|
| `/root/openosx/nix/arm64.nix` | Whole arm64 cross set; `mkArm64Build` at :978; kernel/kexts at :1109-1182; base systems at :1188-1256; package set at :1262-1371 |
| `/root/openosx/nix/image-contents.nix` | arm64 images at :657-706; KCs at :618-627; runners at :851-979; target names at :1005-1021, :1038-1049 |
| `/root/openosx/nix/pkgs/toolchain/kc-arm64.nix` | arm64 boot KC (hardcoded kext list â€” D4) |
| `/root/openosx/nix/lib/kc-kexts.nix` | Shared x86 KC kext list arm64 should be using |
| `/root/openosx/nix/lib/target-info.nix` | `arch`/meson/clang target mapping |
| `/root/openosx/flake.nix` | `arm64CrossToolchain` at :85-88; arm64 re-export at :2636-2801; `arm64Packages` at :3139+; `isDarwin` gating at :3306-3307 |
| `/root/openosx/image.nix` | ESP/GPT layout; `efiBinary`:29, `bootArgs`:33, ESP population :91-93 |
| `/root/openosx/src/Kernel/xnu/pexpert/pexpert/arm64/QEMUVIRT.h` | Board header (UART/GIC/timer/RAM) |
| `/root/openosx/src/Kernel/xnu/pexpert/pexpert/arm64/board_config.h` | `ARM64_BOARD_CONFIG_VIRT`:233, `_T8103`:208, `_VMAPPLE`:243 |
| `/root/openosx/src/Kernel/xnu/config/MASTER.arm64.virt` | arm64-virt kernel config (embedded flavor, no signed-code enforcement) |
| `/root/openosx/src/Kernel/xnu/CMakeLists.txt` *(xnu subdir)* | `OPENOSX_ARM64_MACHINE_CONFIG` handling :17-26 |
| `/root/openosx/src/Kernel/Extensions/PDArmPlatformExpert/` | `IODTPlatformExpert`, single-CPU bring-up, GIC init |
| `/root/openosx/src/Kernel/Extensions/PDArmPCI/PDArmPCI.cpp` | ECAM bridge; hardcoded `0x4010000000` at :10 |
| `C:\â€¦\OpenOSX\tools\xnu-loader\src\jump.S` | EL2â†’EL1 `eret` handoff to XNU `_start` |
| `C:\â€¦\OpenOSX\tools\xnu-loader\src\devtree.c` | arm64 DT: dram/TrustCache :1012-1071, cpus/arm-io/pci/defaults :1218-1328 |
| `C:\â€¦\OpenOSX\tools\xnu-loader\src\boot.c` | `arm64_boot_build_args()` :860-935 |
| `C:\â€¦\OpenOSX\tools\xnu-loader\default.nix` | `BOOTAA64.EFI` naming, `XNU_LOADER_QEMU_VIRT` flag |
| `/root/openosx/tools/testing/boot-test.sh` | x86 harness to clone for arm64 |

## Sources

- [PureDarwin PR #171 â€” Merge arm64 into next](https://github.com/PureDarwin/PureDarwin/pull/171)
- [PureDarwin/xnu-loader commits](https://github.com/PureDarwin/xnu-loader/commits/main)
- [PureDarwin/PureDarwin commits (next)](https://github.com/PureDarwin/PureDarwin/commits/next)
- [PureDarwin issue #137 â€” Tracking issue for boot loader](https://github.com/PureDarwin/PureDarwin/issues/137)
- [Asahi Linux â€” Open OS Platform Interoperability](https://asahilinux.org/docs/platform/open-os-interop/)
- [AsahiLinux/m1n1](https://github.com/AsahiLinux/m1n1)
- [Asahi Linux â€” Progress Report: Linux 7.1 (June 2026)](https://asahilinux.org/2026/06/progress-report-7-1/)
- [QEMU Arm System emulator (virt machine)](https://www.qemu.org/docs/master/system/target-arm.html)
- [alephsecurity/xnu-qemu-arm64](https://github.com/alephsecurity/xnu-qemu-arm64)
- [Booting a macOS Apple Silicon kernel in QEMU â€” Worth Doing Badly](https://worthdoingbadly.com/xnuqemu3/)

