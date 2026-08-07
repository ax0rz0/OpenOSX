{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, wayland
, waylandProtocols
, waylandScanner
, targetTriple ? "x86_64-apple-darwin20.4"
}:

let
  sdkTarball = requireFile {
    name = "MacOSX11.3.sdk.tar.xz";
    sha256 = "9adc1373d3879e1973d28ad9f17c9051b02931674a3ec2a2498128989ece2cb1";
    message = ''
      MacOSX11.3.sdk.tar.xz (Apple SDK, proprietary - not fetchable/redistributable)
      is not yet in your Nix store. Register your local copy with:
        nix-store --add-fixed sha256 /path/to/MacOSX11.3.sdk.tar.xz
    '';
  };
  cc = "${darwinCrossToolchain}/bin/${targetTriple}-clang";
in
stdenv.mkDerivation {
  pname = "openosx-wayland-startup";
  version = "1";
  src = ../../../src/Userspace/wayland-startup;

  dontConfigure = true;
  nativeBuildInputs = [ stdenv.cc waylandScanner ];

  buildPhase = ''
    runHook preBuild
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    wayland-scanner client-header \
      ${waylandProtocols}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
      xdg-shell-client-protocol.h
    wayland-scanner private-code \
      ${waylandProtocols}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
      xdg-shell-protocol.c

    ${cc} -isysroot "$DARWIN_SDK_ROOT" -mmacosx-version-min=11.0 \
      -D_DARWIN_C_SOURCE -I. -I${wayland}/include -I${libSystem}/usr/include \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${wayland}/lib -L${libSystem}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-dylinker_install_name,/usr/lib/dyld \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-fixup_chains \
      -lwayland-client -lSystem -o wayland-startup wayland-startup.c \
      xdg-shell-protocol.c
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 wayland-startup $out/usr/bin/wayland-startup
    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "Minimal visible Wayland startup client for OpenOSX";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
