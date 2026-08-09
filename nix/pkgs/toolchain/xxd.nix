{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, tinyxxd
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
in
stdenv.mkDerivation {
  pname = "openosx-xxd";
  inherit (tinyxxd) version;
  src = tinyxxd.src;

  configurePhase = ''
    runHook preConfigure
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    ${darwinCrossToolchain}/bin/${targetTriple}-clang \
      -std=c11 \
      -isysroot "$DARWIN_SDK_ROOT" \
      -mmacosx-version-min=11.0 \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
      -I${libSystem}/usr/include \
      -fuse-ld=${nativeLd}/bin/ld \
      -nostdlib \
      -Wl,-Z \
      -L${libSystem}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-dylinker_install_name,/usr/lib/dyld \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-undefined,dynamic_lookup \
      -o tinyxxd \
      main.c \
      -lSystem
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$out/bin" "$out/share/man/man1"
    install -m 755 tinyxxd "$out/bin/tinyxxd"
    ln -s tinyxxd "$out/bin/xxd"
    install -m 644 tinyxxd.1 "$out/share/man/man1/xxd.1"
    ln -s xxd.1 "$out/share/man/man1/tinyxxd.1"
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "Tiny xxd-compatible hex dump tool, cross-built for OpenOSX";
    platforms = platforms.linux;
  };
}
