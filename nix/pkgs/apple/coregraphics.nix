{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, targetTriple ? "x86_64-apple-darwin20.4"
, libSystem
, corefoundation
, iokit
, pdgopSrc
, src
}:

# OpenOSX CoreGraphics.framework: the value-type math, the CFType opaque types
# (CGColorSpace, CGColor) built on the shipped CoreFoundation's CFRuntime, and
# the CGDirectDisplay layer over PDGOP's real framebuffer geometry. This is the
# keystone the rest of the GUI frameworks link against; it deliberately ships
# no 2D rasterizer yet (that is the structural CGContext work).

let
  sdkTarball = requireFile {
    name = "MacOSX11.3.sdk.tar.xz";
    sha256 = "cd4f08a75577145b8f05245a2975f7c81401d75e9535dcffbb879ee1deefcbf4";
    message = ''
      MacOSX11.3.sdk.tar.xz (Apple SDK, proprietary - not fetchable/redistributable)
      is not yet in your Nix store. Register your local copy with:
        nix-store --add-fixed sha256 /path/to/MacOSX11.3.sdk.tar.xz
    '';
  };

  installName = "/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics";
in
stdenv.mkDerivation {
  pname = "openosx-coregraphics";
  version = "0.1";

  inherit src;

  buildPhase = ''
    runHook preBuild

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    # Our CoreFoundation installs its headers (including the private CFRuntime.h)
    # flattened into $out/include, with no CoreFoundation/ subdir. Stage a
    # "CoreFoundation/" symlink so <CoreFoundation/CFRuntime.h> resolves to ours
    # rather than the SDK's (which has no CFRuntime.h) - the same trick
    # foundation.nix uses.
    mkdir -p cf-headers
    ln -s ${corefoundation}/include cf-headers/CoreFoundation

    # PDGOP is not a standalone package - it is a ~170-line IOKit framebuffer
    # client built inside the CMake tree - so compile its source straight into
    # CoreGraphics. It links against IOKit like ioreg/iomediacheck do.
    ${darwinCrossToolchain}/bin/${targetTriple}-clang \
      -isysroot "$DARWIN_SDK_ROOT" -dynamiclib \
      -Icf-headers \
      -I${src}/include \
      -I${pdgopSrc}/include \
      -I${libSystem}/usr/include \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib -Wl,-Z \
      -L${libSystem}/usr/lib -L${corefoundation}/usr/lib -L${iokit}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-dylinker_install_name,/usr/lib/dyld \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-install_name,${installName} \
      -Wl,-exported_symbols_list,${src}/CoreGraphics.exports \
      ${src}/src/CGGeometry.c \
      ${src}/src/CGColorSpace.c \
      ${src}/src/CGColor.c \
      ${src}/src/CGDisplay.c \
      ${pdgopSrc}/PDGOP.c \
      -lIOKitCF -lCoreFoundation -lSystem \
      -o CoreGraphics

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    frameworkDir="$out/System/Library/Frameworks/CoreGraphics.framework"
    mkdir -p "$frameworkDir/Versions/A"
    cp CoreGraphics "$frameworkDir/Versions/A/CoreGraphics"
    ln -s A "$frameworkDir/Versions/Current"
    ln -s Versions/Current/CoreGraphics "$frameworkDir/CoreGraphics"

    # Flat /usr/lib alias, like OpenGL.framework, for -lCoreGraphics linkers and
    # for the two-level-namespace path a bottle uses.
    mkdir -p "$out/usr/lib"
    ln -s "../../System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
      "$out/usr/lib/libCoreGraphics.dylib"

    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "OpenOSX CoreGraphics.framework: geometry, CGColor/CGColorSpace CFTypes, and CGDirectDisplay over PDGOP";
    platforms = platforms.linux;
  };
}
