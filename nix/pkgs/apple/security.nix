{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, corefoundation
, openssl
, src
, targetTriple ? "x86_64-apple-darwin20.4"
}:

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

  installName = "/System/Library/Frameworks/Security.framework/Versions/A/Security";
in
stdenv.mkDerivation {
  pname = "openosx-security";
  version = "0.1";

  dontUnpack = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    # CoreFoundation's headers are reached as <CoreFoundation/...>, so give the
    # compiler a directory whose *child* is named CoreFoundation - the same
    # symlink trick CoreGraphics/CoreVideo/Metal use.
    mkdir -p cf-headers
    ln -s ${corefoundation}/include cf-headers/CoreFoundation

    # SecureTransport.c compiles against the SDK's own Security headers, so it
    # uses Apple's enum names directly and static-asserts the values st_core.h
    # transcribed by hand. st_core.c is the only file that sees OpenSSL.
    ${darwinCrossToolchain}/bin/${targetTriple}-clang \
      -isysroot "$DARWIN_SDK_ROOT" -dynamiclib \
      -I${src}/include -I$PWD/cf-headers -I${openssl}/include \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${corefoundation}/usr/lib \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-install_name,${installName} \
      -Wl,-force_load,${openssl}/lib/libssl.a \
      -Wl,-force_load,${openssl}/lib/libcrypto.a \
      -lCoreFoundation -lSystem \
      ${src}/Security.c \
      ${src}/securetransport/st_core.c \
      ${src}/securetransport/SecureTransport.c \
      -o Security

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    frameworkDir="$out/System/Library/Frameworks/Security.framework"
    mkdir -p "$frameworkDir/Versions/A/Headers"
    cp Security "$frameworkDir/Versions/A/Security"
    cp -a ${src}/include/Security/. "$frameworkDir/Versions/A/Headers/"

    ln -s A "$frameworkDir/Versions/Current"
    ln -s Versions/Current/Security "$frameworkDir/Security"
    ln -s Versions/Current/Headers "$frameworkDir/Headers"

    # Also drop a flat dylib under /usr/lib, matching every other
    # OpenOSX library (CoreFoundation, IOKitCF) - some consumers link
    # -lSecurity / -L.../usr/lib rather than -F.../Frameworks.
    mkdir -p "$out/usr/lib"
    ln -s "../../System/Library/Frameworks/Security.framework/Versions/A/Security" \
      "$out/usr/lib/libSecurity.dylib"

    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "Minimal OpenOSX Security.framework (real SecRandomCopyBytes, stub SecItem*)";
    platforms = platforms.linux;
  };
}
