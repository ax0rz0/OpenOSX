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

    CLANG="${darwinCrossToolchain}/bin/${targetTriple}-clang"

    # Compiled separately, and the include paths are the reason. OpenOSX ships
    # its own cut-down <Security/SecBase.h>, which has no SecCertificateRef or
    # SecKeyRef in it. Putting -I${src}/include on SecureTransport.c's line lets
    # that header win over the SDK's, and the SDK's own SecCertificate.h then
    # fails to parse against it. So Security.c gets our headers, and the
    # Secure Transport files see only the SDK's.
    $CLANG -isysroot "$DARWIN_SDK_ROOT" -c \
      -I${src}/include -I$PWD/cf-headers \
      ${src}/Security.c -o Security.o

    # st_core.c is the only file here that sees OpenSSL; it has no
    # CoreFoundation and no Security headers at all, by design.
    $CLANG -isysroot "$DARWIN_SDK_ROOT" -c \
      -I${openssl}/include \
      ${src}/securetransport/st_core.c -o st_core.o

    # SecureTransport.c compiles against the SDK's Security headers, so it uses
    # Apple's enum names directly and static-asserts the values st_core.h
    # transcribed by hand.
    $CLANG -isysroot "$DARWIN_SDK_ROOT" -c \
      -I$PWD/cf-headers \
      ${src}/securetransport/SecureTransport.c -o SecureTransport.o

    # -exported_symbols_list + -dead_strip. OpenSSL is linked in statically, and
    # without the allow-list its 10,728 symbols - every X509_*, EVP_*, BIO_* and
    # SSL_* - are exported from Security.framework. An app carrying its own
    # OpenSSL could then bind to ours, and version-skewed crypto that silently
    # half-works is a bad way to find that out. -dead_strip then drops the
    # OpenSSL nothing reaches, which is most of it.
    #
    # No -force_load: the whole point is to pull in only what st_core.c uses.
    $CLANG -isysroot "$DARWIN_SDK_ROOT" -dynamiclib \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${corefoundation}/usr/lib \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-install_name,${installName} \
      -Wl,-exported_symbols_list,${src}/Security.exports \
      -Wl,-dead_strip \
      -lCoreFoundation -lSystem \
      Security.o st_core.o SecureTransport.o \
      ${openssl}/lib/libssl.a ${openssl}/lib/libcrypto.a \
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
