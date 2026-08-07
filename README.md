# OpenOSX

OpenOSX is an open-source operating system built on Darwin — the OS foundation
underneath macOS — with one uncompromising rule: **everything we ship is built
from open source**. Kernel, drivers, bootloader, userland: no closed Apple
binaries anywhere in the boot chain.

The goal is an OS image that boots easily in common VMs (QEMU, VirtualBox,
VMware) from a fully open toolchain: the XNU kernel and kernel extensions in
this repo, loaded by an open EFI bootloader
([xnu-loader](https://github.com/PureDarwin/xnu-loader)), up to a real
userland — launchd, a shell, X11/Wayland, and a desktop environment.

## Heritage

OpenOSX is a successor to [PureDarwin](https://github.com/PureDarwin/PureDarwin)
([puredarwin.org](https://www.puredarwin.org)), the community project that kept
the dream of a usable standalone Darwin alive after OpenDarwin, and before that
to Apple's own open-source Darwin releases. Substantial portions of this tree
were authored by the OpenOSX developers and by Apple; see
[PUREDARWIN_LICENSE.txt](PUREDARWIN_LICENSE.txt), [APPLE_LICENSE.txt](APPLE_LICENSE.txt),
[APPLE_DRIVER_LICENSE.txt](APPLE_DRIVER_LICENSE.txt), and
[docs/OPENOSX_ATTRIBUTION.md](docs/OPENOSX_ATTRIBUTION.md). We are
grateful to both.

## Building OpenOSX

The supported build is **Nix**, on Linux or macOS (x86_64 or aarch64):

```sh
# One-time: the Apple SDK tarball is proprietary and cannot be fetched by Nix.
# Obtain MacOSX11.3.sdk.tar.xz and register it (Nix verifies its sha256):
nix-store --add-fixed sha256 /path/to/MacOSX11.3.sdk.tar.xz

# Build a bootable disk image (GPT: EFI system partition + root filesystem):
nix build .#image-minimal    # lean image: kernel, launchd, zsh, toybox
nix build .#image            # full image: X11/Wayland, XFCE, and friends
```

Run the result in QEMU:

```sh
nix run .#vm                 # boots result/openosx.img with serial on stdio
```

A classic CMake build of individual components on macOS also works; see
`CMakeLists.txt` and the CI workflows.

It should be noted that aarch64 support is an extreme work in progress and may break often.

## What's in the tree

- `src/Kernel/xnu` — the XNU kernel (Darwin 20.5 lineage, x86_64 + arm64)
- `src/Kernel/Extensions` — kernel extensions: ACPI, APIC, PCI, PS/2, HID,
  IDE/AHCI/NVMe, USB (UHCI/OHCI/EHCI/xHCI), e1000 + VirtIO net, framebuffers
  (GOP, VirtIO GPU, Intel Gen9), and filesystems (HFS+, ext4, APFS, msdosfs)
- `src/Libraries` — libSystem, CoreFoundation, dyld, launchd/XPC, objc4, and
  the rest of the userland library stack
- `src/Userspace` — userland programs (including fbDOOM, optionally)
- `nix/`, `flake.nix`, `image.nix` — the cross-compilation and image pipeline
- `tools` — host toolchain (cctools/ld64, mig, xar, kc-tools, xnu-loader)

## End Goal & Author Notes
OpenOSX is designed to be binary compatible with *macOS* whilst maintaining a pure FOSS design, the end goal of OpenOSX is
to be able to run most *macOS* apps directly on a FOSS operating system without proprietary hardware (and maybe be superior to linux, or never).


## License

Code inherited from Apple is under the
[Apple Public Source License](APPLE_LICENSE.txt) (drivers:
[APPLE_DRIVER_LICENSE.txt](APPLE_DRIVER_LICENSE.txt)). Code authored by the
OpenOSX project is under the [OpenOSX license](PUREDARWIN_LICENSE.txt).
New OpenOSX code is under the same terms as the OpenOSX license unless
noted otherwise.
