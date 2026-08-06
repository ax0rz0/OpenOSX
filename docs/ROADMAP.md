# OpenOSX Engineering Roadmap

**Revision 2 — next-base.** OpenOSX is based on upstream PureDarwin's `next`
branch, which already builds a bootable image (kernel, kexts, launchd, shell,
X11/Wayland, XFCE) entirely from open source via the Nix flake, on Linux or
macOS. The original bring-up roadmap (rev 1, on the `openosx-dev` branch)
is superseded: its M1–M4 milestones exist upstream; our work is verification,
rebranding, hardening, and extension.

## Build & test loop

- Build: `nix build .#image-minimal` (lean) / `.#image` (full XFCE) — WSL2
  Ubuntu or any Linux/macOS with Nix. Requires the `MacOSX11.3.sdk.tar.xz`
  registered via `nix-store --add-fixed sha256` (hash pinned in
  `nix/pkgs/toolchain/*.nix`).
- Run: `nix run .#vm` (QEMU q35, TCG, IvyBridge masquerading as GenuineIntel —
  boots regardless of AMD host CPU), serial on stdio. Headless verdicts: grep
  the serial log for the Darwin banner / launchd / panic signatures.
- The deep rename is replayable after upstream syncs: `perl tools/rename-openosx.pl`
  (guards: copyright lines, license files/markers, `github:PureDarwin` inputs,
  vendored subtrees). See `docs/PUREDARWIN_ATTRIBUTION.md`.

## Near-term

1. Verify vanilla `next` image boots in QEMU (serial banner → launchd → shell).
2. Rebuild the renamed `openosx-next` branch; verify identical boot.
3. Rebrand the boot experience (image name, banners, fastfetch logo, hostname
   defaults) — cosmetic-level, guarded from license/attribution text.
4. VirtualBox validation (EFI, PIIX3/ICH9 profile) once QEMU is green; then
   VMware. Document per-VM settings for "boots easily on most VMs."
5. GitHub Actions CI (macOS runner) as the clean-licensed second build path
   once the account's billing lock clears; Linux runner + Nix as a fast lane.
6. XFCE image (`.#image`) boot + screenshot tour.

## Post-M4 / bare-metal era

### Bare-metal hardware via BSD driver ports

VM targets are covered in-tree (e1000, VirtIO net/GPU, AHCI, xHCI, PS/2).
For real hardware, the donor pool is FreeBSD/OpenBSD (BSD/ISC licenses mix
cleanly with our APSL/BSD kernel and can ship in the image; Linux/GPL drivers
cannot be linked into kexts):

- **Port the hardware core, rewrite the shell:** keep register/DMA/firmware/
  errata logic; rewrite bus attach (newbus → IOKit/IOPCIDevice), interrupts
  (→ IOInterruptEventSource on a workloop), DMA (busdma → IODMACommand), NIC
  attach (ifnet → IOEthernetController).
- **Build one reusable bsd-compat shim first** (busdma→IODMACommand,
  callout→IOTimerEventSource, mbuf adapters) so each subsequent port gets
  cheaper. itlwm's compat layer is the template.
- **WiFi:** Apple's IO80211Family is closed — follow the itlwm/OpenIntelWireless
  precedent: port OpenBSD's net80211 stack + iwm/iwx drivers into an IOKit
  kext presenting as Ethernet. (Check itlwm's glue-code license before
  vendoring; OpenBSD core is ISC.)
- **Intel shared-code NICs** (e1000/igb/ixgbe) are designed for OS-independent
  reuse — PDE1000 in-tree already follows this pattern; igb/ixgbe follow it.
- **GPUs do not port** — bare-metal graphics stays framebuffer (GOP / Gen9
  in-tree) for the foreseeable future.

### Other post-M4 items

- Real ACPI platform expert on x86 (upstream PDACPIPlatform) where applicable.
- Audio (RavynHDAudio in-tree) validation.
- Upstream sync cadence: merge `PureDarwin/next` regularly + re-run rename.
