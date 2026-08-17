{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, targetTriple ? "x86_64-apple-darwin20.4"
, libSystem
, src
}:

# OpenOSX Cocoa.framework: the umbrella, empty on purpose.
#
# 33 binaries in the measured corpus - the whole JDK toolchain - link Cocoa and
# import no symbol from it. They fail today because the framework is absent, not
# because of anything they use, so a dylib with the right install name and no
# code is the entire fix. See src/Libraries/Cocoa/Cocoa.c for why it does not
# re-export AppKit or Foundation yet.

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

  cc = "${darwinCrossToolchain}/bin/${targetTriple}-clang";
  installName = "/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa";
in
stdenv.mkDerivation {
  pname = "openosx-cocoa";
  version = "0.1";

  inherit src;

  buildPhase = ''
    runHook preBuild

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    ${cc} -isysroot "$DARWIN_SDK_ROOT" -dynamiclib \
      -I${libSystem}/usr/include \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-install_name,${installName} \
      -Wl,-fixup_chains \
      ${src}/Cocoa.c \
      -lSystem \
      -o Cocoa

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    frameworkDir="$out/System/Library/Frameworks/Cocoa.framework"
    mkdir -p "$frameworkDir/Versions/A"
    cp Cocoa "$frameworkDir/Versions/A/Cocoa"
    ln -s A "$frameworkDir/Versions/Current"
    ln -s Versions/Current/Cocoa "$frameworkDir/Cocoa"

    mkdir -p "$out/usr/lib"
    ln -s "../../System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa" \
      "$out/usr/lib/libCocoa.dylib"

    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "OpenOSX Cocoa.framework umbrella - empty, so Cocoa-linking binaries load";
    platforms = platforms.linux;
  };
}
