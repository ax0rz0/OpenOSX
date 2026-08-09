{ stdenv
, lib
, meson
, ninja
, pkg-config
, python3
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, corefoundation
, iokit
, puredarwinSource
, src
, targetTriple ? "x86_64-apple-darwin20.4"
}:

let
  targetInfo = import ../../lib/target-info.nix targetTriple;
  sdkTarball = requireFile {
    name = "MacOSX11.3.sdk.tar.xz";
    sha256 = "cd4f08a75577145b8f05245a2975f7c81401d75e9535dcffbb879ee1deefcbf4";
    message = "Register the local MacOSX11.3.sdk.tar.xz with nix-store.";
  };
in
stdenv.mkDerivation {
  pname = "openosx-libdrm";
  version = "2.4.133";
  inherit src;

  nativeBuildInputs = [ meson ninja pkg-config python3 ];

  postPatch = ''
    mkdir -p compat/sys
    cat > compat/sys/sysmacros.h <<EOF
#ifndef OPENOSX_SYSMACROS_H
#define OPENOSX_SYSMACROS_H
#include <sys/types.h>
#ifndef major
#define major(dev) ((int)(((dev) >> 24) & 0xff))
#endif
#ifndef minor
#define minor(dev) ((int)((dev) & 0xffffff))
#endif
#ifndef makedev
#define makedev(major, minor) ((dev_t)((((dev_t)(major)) << 24) | ((dev_t)(minor) & 0xffffff)))
#endif
#endif
EOF
    substituteInPlace meson.build \
      --replace "['windows', 'darwin']" "['windows']"
    cp ${puredarwinSource}/xf86drmPuredarwin.c .
    cp ${puredarwinSource}/xf86drmPuredarwin.h .
    substituteInPlace meson.build \
      --replace "'xf86drmMode.c'" "'xf86drmMode.c', 'xf86drmPuredarwin.c'" \
      --replace "install_headers('libsync.h', 'xf86drm.h', 'xf86drmMode.h')" \
                "install_headers('libsync.h', 'xf86drm.h', 'xf86drmMode.h', 'xf86drmPuredarwin.h')"
  '';

  configurePhase = ''
    runHook preConfigure
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    cat > openosx-cross.ini <<EOF
[binaries]
c = '${darwinCrossToolchain}/bin/${targetTriple}-clang'
ar = '${darwinCrossToolchain}/bin/${targetTriple}-ar'
strip = '${darwinCrossToolchain}/bin/${targetTriple}-strip'
pkg-config = '${pkg-config}/bin/pkg-config'

[built-in options]
c_args = ['-isysroot', '$PWD/sdk/MacOSX11.3.sdk', '-mmacosx-version-min=11.0', '-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=0', '-fno-stack-protector', '-I${libSystem}/usr/include', '-I$PWD/compat']
c_link_args = ['-isysroot', '$PWD/sdk/MacOSX11.3.sdk', '-mmacosx-version-min=11.0', '-F$PWD/sdk/MacOSX11.3.sdk/System/Library/Frameworks', '-fuse-ld=${nativeLd}/bin/ld', '-nostdlib', '-L${libSystem}/usr/lib', '-L${corefoundation}/usr/lib', '-L${iokit}/usr/lib', '-Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib', '-Wl,-platform_version,macos,11.0,11.5', '-lIOKitCF', '-lCoreFoundation', '-lSystem']

[host_machine]
system = 'darwin'
cpu_family = '${targetInfo.mesonCpuFamily}'
cpu = '${targetInfo.mesonCpu}'
endian = '${targetInfo.mesonEndian}'

[properties]
needs_exe_wrapper = true
EOF
    meson setup build \
      --cross-file openosx-cross.ini \
      --prefix=/ \
      --libdir=lib \
      --buildtype=release \
      -Ddefault_library=both \
      -Damdgpu=disabled \
      -Dintel=disabled \
      -Dnouveau=disabled \
      -Dradeon=disabled \
      -Dvmwgfx=disabled \
      -Dtests=false \
      -Dman-pages=disabled
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    ninja -C build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    DESTDIR=$out ninja -C build install
    # The image flattens this package under /, but dependent cross-builds
    # consume the pkg-config file from the Nix store first.
    substituteInPlace $out/lib/pkgconfig/libdrm.pc \
      --replace-fail 'prefix=/' "prefix=$out"
    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "OpenOSX userspace DRM ABI and format definitions";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
