{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, targetTriple ? "x86_64-apple-darwin20.4"
, libSystem
, corefoundation
, src
}:

# OpenOSX CoreVideo.framework: CVDisplayLink as a 60 Hz timer thread (a
# faithful stand-in where the framebuffer exposes no vblank). Malloc-backed
# CVPixelBuffer and the kCV* constants come later; this first cut is what The
# Powder Toy imports (the seven CVDisplayLink symbols).

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

  installName = "/System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo";
in
stdenv.mkDerivation {
  pname = "openosx-corevideo";
  version = "0.1";

  inherit src;

  buildPhase = ''
    runHook preBuild

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    ${darwinCrossToolchain}/bin/${targetTriple}-clang \
      -isysroot "$DARWIN_SDK_ROOT" -dynamiclib \
      -I${corefoundation}/usr/include \
      -I${libSystem}/usr/include \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib -Wl,-Z \
      -L${libSystem}/usr/lib -L${corefoundation}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-dylinker_install_name,/usr/lib/dyld \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-install_name,${installName} \
      -Wl,-exported_symbols_list,${src}/CoreVideo.exports \
      ${src}/src/CVDisplayLink.c \
      -lCoreFoundation -lSystem \
      -o CoreVideo

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    frameworkDir="$out/System/Library/Frameworks/CoreVideo.framework"
    mkdir -p "$frameworkDir/Versions/A"
    cp CoreVideo "$frameworkDir/Versions/A/CoreVideo"
    ln -s A "$frameworkDir/Versions/Current"
    ln -s Versions/Current/CoreVideo "$frameworkDir/CoreVideo"

    mkdir -p "$out/usr/lib"
    ln -s "../../System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo" \
      "$out/usr/lib/libCoreVideo.dylib"

    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "OpenOSX CoreVideo.framework: CVDisplayLink timer over a software framebuffer";
    platforms = platforms.linux;
  };
}
