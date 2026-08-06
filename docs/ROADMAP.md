# OpenOSX Engineering Roadmap

**Goal:** boot OpenOSX — kernel, drivers, bootloader, userland all built from
open source — headlessly in VMs, up to an interactive shell, then iterate
toward a simple desktop environment.

Build convention: all compiling happens on GitHub Actions `macos-latest`
(the tree only builds on macOS). All VM testing happens on the Windows host
via downloaded artifacts.

## Boot architecture (decided)

**Primary:** [PureDarwin/xnu-loader](https://github.com/PureDarwin/xnu-loader)
(BSD-3, GNU-EFI `BOOTX64.EFI`) loading a classic **prelinkedkernel** built by
[PureDarwin/kc-tools](https://github.com/PureDarwin/kc-tools) from a FAT ESP
under UEFI (OVMF / VirtualBox EFI). xnu-loader implements the XNU x86_64
handoff this tree expects (boot_args v2, device tree with
`/chosen/random-seed`, fake EFI system table, 64→32-bit drop to `pstart`),
and the classic prelinkedkernel path is verified alive in our
`libsa/bootstrap.cpp` / `pexpert/gen/kcformat.c`. No Apple `boot.efi`
anywhere in the chain (which rules out chain-loaders like OpenCore/Clover).

**Fallback:** [ravynsoft/efiloader](https://github.com/ravynsoft/efiloader)
with the same prelinkedkernel payload. Escape hatch for debugging: raw kernel
with `config_kxld` re-enabled + boot-132-style driver injection.

## VM iterate loop (decided)

**QEMU TCG** on the Windows host, emulating an Intel Penryn — this presents
`GenuineIntel` CPUID and benign MSR behavior, so the unmodified kernel boots
regardless of the AMD host CPU, and it coexists with Hyper-V:

```
qemu-system-x86_64 -machine pc -accel tcg -cpu Penryn,+ssse3,+sse4.1,+sse4.2,+popcnt
  -smp 2 -m 2048
  -drive if=pflash,format=raw,readonly=on,file=<OVMF code fd>
  -drive file=esp.img,format=raw,if=ide
  -serial file:serial.log -display none -no-reboot
```

Kernel boot-args, always: `-v serial=3 debug=0x8` — serial COM1 (0x3F8) is the
single source of truth for pass/fail grepping.

**AMD mitigation** (lands in P2, boot-arg-gated): demote the GenuineIntel
panic in `osfmk/i386/cpuid.c` to a warning with Penryn fallback; `tscfreq=`/
`busfreq=` boot-args in `osfmk/i386/tsc.c`; vendor-guard the unconditional
`rdmsr` in commpage init; generic core count via CPUID 0xB. Then VirtualBox
(EFI, PIIX3, `--cpu-profile "Intel Core i7-3960X"`, UART→file) becomes the
secondary loop.

**Standard hardware profile** (all VM configs and docs): i440FX/PIIX3, IDE/ATA
storage only, PS/2 input, I/O APIC on, EFI loading `\EFI\BOOT\BOOTX64.EFI`,
serial COM1. No NIC yet (e1000 kext is future work).

## Phases

### P0 — Deep rename + CI + harness (done/in progress)
1. ✅ CMake dir-var decoupling + `project(OPENOSX)`
2. ✅ `__PUREDARWIN__` → `__OPENOSX__` (all 6 sites atomically)
3. ✅ `org.puredarwin.*` → `org.openosx.*` bundle IDs (3 kexts; `com.apple.*` untouched — kpi/IOKit matching)
4. ✅ Branding sweep with legal allowlist (`docs/PUREDARWIN_ATTRIBUTION.md`)
5. CI artifacts: kernel, kexts.tar, staged install tar, logs — deterministic names
6. Local: QEMU + OVMF install, `test-boot.ps1` (launch → poll serial.log → pass/fail regex → exit code)

### P1 — Kernel first-light (M1)
7. Vendor `xnu-loader` → `tools/xnu-loader/`, `kc-tools` → `tools/kc-tools/` (git subtree, BSD-3); wire into CMake; gnu-efi via brew; mind the ms_abi calling-convention landmine.
8. CI ESP assembly with mtools: FAT32 `esp.img` + `BOOTX64.EFI` + raw RELEASE kernel + boot-args.
9. **M1 = `serial.log` shows `Darwin Kernel Version 20.5.0` … ending in `panic: Process 1 exec of /sbin/launchd failed`.** That panic is success: the kernel ran all of `bsd_init`.
10. Debug loader gaps via serial `kprintf` (lights up in `vstart()` almost immediately).

### P2 — Kexts + rootfs (M2)
11. Prelinkedkernel from kernel + kexts in dependency order (IOACPIFamily → AppleI386GenericPlatform → AppleAPIC → IOPCIFamily → IOATAFamily → AppleIntelPIIXATA → IOStorageFamily → ApplePS2Controller → corecrypto → pthread); verify `__PRELINK_INFO` before booting.
12. AMD source edits land; first VirtualBox EFI validation boot.
13. HFS+ : vendor `apple-oss-distributions/hfs` (hfs-556.60.1) into the prelinkedkernel; host-side unprivileged `newfs_hfs` (Darnix patches) + xpwn `hfsplus` (GPL — CI host tool only, never shipped) to build `ramdisk.img`.
14. **M2 = serial shows root mount on `md0`** (xnu-loader publishes RAMDisk via `/chosen/memory-map` → `mdevadd()`; boot-arg `rd=md0`), PID-1 panic becomes errno 2 (root mounted, `/sbin/launchd` missing).

### P3 — PID 1 (M3)
15. `src/protoinit/` (~200 lines C, static: Csu crt0 + raw syscall stubs, no libc/dyld) installed as `/sbin/launchd` (path is hardcoded in the kernel). Banner to `/dev/console`, `setsid()`+`TIOCSCTTY`, `waitpid` loop forever (PID 1 must never exit).
16. **M3 = `OpenOSX protoinit` banner on serial + 60s panic-free.** Optional: `mockfs` build to bisect fs bugs from init bugs.

### P4 — Shell (M4)
17. Static libc subset: Apple Libc (Big Sur drop) stdio/string/stdlib + libsystem_malloc/pthread/platform + `libsystem_kernel_static` + crt0.
18. `mksh` static as `/bin/sh` (then Apple bash-3.2 + shell_cmds/file_cmds). Serial becomes bidirectional (`-serial tcp:…`).
    **M4 = expect-script sends `echo OPENOSX_ALIVE` over serial and sees the echo.**

Post-M4: dyld + libSystem umbrella + libdispatch, launchd-842.92.1 (+ nextbsd patches) replacing protoinit, e1000 NIC, AHCI (PureDarwin/IOAHCIProject), PDACPIPlatform, DE research.

## Top risks

| Risk | Mitigation |
|---|---|
| AMD host (cpuid panic, MSR #GP) | QEMU TCG day 1; 4 gated source edits in P2; never trust hypervisor CPUID masking |
| kc-tools immature | Verify every image before booting; raw-kernel boot as bisection baseline; efiloader fallback |
| No HFS/APFS in kernel; imageboot needs closed kext | HFS+ kext in prelinkedkernel; `mdevadd()` ramdisk, never `root-dmg`; NFS root as fallback |
| launchd closed post-10.9 | protoinit through M4; port launchd-842 later |
| libSystem gaps / no dyld | static-only userland through M4 |
| VBox EFI quirks under Hyper-V | QEMU is the reference target; VBox validation-only until AMD edits land |
| Host disk (~22 GB free) + OneDrive sync thrash | all images/logs in `C:\openosx-test\` outside OneDrive; small images (ESP 64 MB, ramdisk ≤256 MB) |
| CI limits (metered macOS minutes, 14-day artifacts) | cache xnu objdir; split loader-only vs kernel jobs; tag milestone builds as Releases |
