{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, corefoundation
, iokit
, targetTriple ? "x86_64-apple-darwin20.4"
}:

let
  sdkTarball = requireFile {
    name = "MacOSX11.3.sdk.tar.xz";
    sha256 = "9adc1373d3879e1973d28ad9f17c9051b02931674a3ec2a2498128989ece2cb1";
    message = "Register the local MacOSX11.3.sdk.tar.xz with nix-store.";
  };
  cc = "${darwinCrossToolchain}/bin/${targetTriple}-clang";
in
stdenv.mkDerivation {
  pname = "openosx-pdsurface";
  version = "1";
  src = ../../../src/Libraries/PDSurface;

  dontConfigure = true;
  nativeBuildInputs = [ stdenv.cc ];

  buildPhase = ''
    runHook preBuild
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    ${cc} -isysroot "$DARWIN_SDK_ROOT" -mmacosx-version-min=11.0 \
      -D_DARWIN_C_SOURCE -Iinclude \
      -I${libSystem}/usr/include -I${corefoundation}/usr/include \
      -I${iokit}/usr/include \
      -dynamiclib -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${corefoundation}/usr/lib -L${iokit}/usr/lib \
      -Wl,-install_name,/usr/lib/libPDSurface.dylib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,11.0,11.5 -Wl,-fixup_chains \
      -lIOKitCF -lCoreFoundation -lSystem \
      -o libPDSurface.dylib PDSurface.c
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 libPDSurface.dylib $out/usr/lib/libPDSurface.dylib
    install -Dm644 include/PDSurface.h $out/usr/include/PDSurface.h
    install -Dm644 include/PDSurfaceProtocol.h $out/usr/include/PDSurfaceProtocol.h

    mkdir -p $out/usr/lib/pkgconfig
    cat > $out/usr/lib/pkgconfig/pdsurface.pc <<EOF
    prefix=/usr
    libdir=$out/usr/lib
    includedir=$out/usr/include

    Name: PDSurface
    Description: OpenOSX shareable graphics buffers
    Version: 1
    Libs: -L\''${libdir} -lPDSurface
    Cflags: -I\''${includedir}
    EOF
    sed -i 's/^    //' $out/usr/lib/pkgconfig/pdsurface.pc
    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "OpenOSX shareable graphics buffers, driver independent";
    license = licenses.bsd3;
    platforms = platforms.linux;
  };
}
