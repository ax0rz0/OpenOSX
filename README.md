# OpenOSX

OpenOSX is an open-source operating system built on Darwin — the OS foundation
underneath macOS — with one uncompromising rule: **everything we ship is built
from open source**. Kernel, drivers, bootloader, userland: no closed Apple
binaries anywhere in the boot chain.

The current goal is a small OS image that boots easily in common VMs
(QEMU, VirtualBox, VMware) from a 100% open toolchain: the XNU kernel and
kernel extensions in this repo, loaded by an open EFI bootloader, up to an
interactive shell — and eventually a simple desktop environment.

## Heritage

OpenOSX is a successor to [PureDarwin](https://github.com/PureDarwin/PureDarwin)
([puredarwin.org](https://www.puredarwin.org)), the community project that kept
the dream of a usable standalone Darwin alive after OpenDarwin, and before that
to Apple's own open-source Darwin releases. Substantial portions of this tree
were authored by the PureDarwin developers and by Apple; see
[PUREDARWIN_LICENSE.txt](PUREDARWIN_LICENSE.txt), [APPLE_LICENSE.txt](APPLE_LICENSE.txt),
and [APPLE_DRIVER_LICENSE.txt](APPLE_DRIVER_LICENSE.txt). We are grateful to both.

## Building OpenOSX

To build OpenOSX, you will need OpenSSL installed, which is used by xar and ld64.
OpenOSX builds only on macOS. It is currently tested with Xcode 14, but should work
with any other modern Xcode.

You will also need zlib, which is used by the DTrace CTF tools used in building the kernel.

```sh
mkdir build && cd build
cmake -G Ninja -DKERNEL_BUILD_XNU=1 -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" ..
ninja
```

Continuous builds run on GitHub Actions (`.github/workflows/build-dev.yml`) and
upload the kernel, kexts, and logs as artifacts of every run.

## What's in the tree

- `src/Kernel/xnu` — the XNU kernel (Darwin 20.5 lineage, x86_64)
- `src/Kernel/Extensions` — kernel extensions for generic PC/VM hardware
  (ACPI, APIC, PCI, PS/2, IDE/ATA, storage, corecrypto, pthread)
- `src/Libraries` — libSystem components (libc, libdispatch, libmalloc, …)
- `tools` — host toolchain (cctools/ld64, mig, xar, dtrace CTF tools)

## License

Code inherited from Apple is under the
[Apple Public Source License](APPLE_LICENSE.txt) (drivers:
[APPLE_DRIVER_LICENSE.txt](APPLE_DRIVER_LICENSE.txt)). Code authored by the
PureDarwin project is under the [PureDarwin license](PUREDARWIN_LICENSE.txt).
New OpenOSX code is under the same terms as the PureDarwin license unless
noted otherwise.
