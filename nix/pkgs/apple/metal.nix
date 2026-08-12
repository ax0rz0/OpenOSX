{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, targetTriple ? "x86_64-apple-darwin20.4"
, libSystem
, libobjc
, corefoundation
, foundation
, src
}:

# OpenOSX Metal.framework: the nil-device probe stub plus five empty
# MTL*Descriptor classes, so a Metal-probing app (SDL2 / The Powder Toy) loads
# and then falls back to OpenGL. No Metal driver; see src/Libraries/Metal/src.

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
  installName = "/System/Library/Frameworks/Metal.framework/Versions/A/Metal";
in
stdenv.mkDerivation {
  pname = "openosx-metal";
  version = "0.1";

  inherit src;

  buildPhase = ''
    runHook preBuild

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    mkdir -p fheaders/Foundation
    cp ${foundation}/usr/include/Foundation/*.h fheaders/Foundation/ 2>/dev/null || true

    ${cc} -isysroot "$DARWIN_SDK_ROOT" -dynamiclib \
      -x objective-c -fno-objc-arc -fobjc-runtime=macosx \
      -Ifheaders \
      -I${foundation}/usr/include \
      -I${corefoundation}/include \
      -I${libobjc}/usr/include \
      -I${libSystem}/usr/include \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libobjc}/usr/lib \
      -L${corefoundation}/usr/lib -L${foundation}/usr/lib \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-install_name,${installName} \
      -Wl,-fixup_chains \
      ${src}/src/Metal.m \
      -lobjc -lFoundation -lCoreFoundation -lSystem \
      -o Metal

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    frameworkDir="$out/System/Library/Frameworks/Metal.framework"
    mkdir -p "$frameworkDir/Versions/A"
    cp Metal "$frameworkDir/Versions/A/Metal"
    ln -s A "$frameworkDir/Versions/Current"
    ln -s Versions/Current/Metal "$frameworkDir/Metal"

    mkdir -p "$out/usr/lib"
    ln -s "../../System/Library/Frameworks/Metal.framework/Versions/A/Metal" \
      "$out/usr/lib/libMetal.dylib"

    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "OpenOSX Metal.framework: nil-device probe stub so Metal-probing apps fall back to GL";
    platforms = platforms.linux;
  };
}
