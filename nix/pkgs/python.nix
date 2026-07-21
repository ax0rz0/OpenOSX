{ stdenv
, lib
, requireFile
, gnumake
, pkg-config
, darwinCrossToolchain
, nativeLd
, libSystem
, python3
, zlib
, openssl
, libffi
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
in
stdenv.mkDerivation {
  pname = "puredarwin-python";
  inherit (python3) version src;

  nativeBuildInputs = [ gnumake pkg-config python3 ];

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"
    export CC="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang"
    export CXX="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang++"
    export AR="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ar"
    export RANLIB="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ranlib"
    export STRIP="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-strip"
    export PKG_CONFIG_LIBDIR="${zlib}/lib/pkgconfig:${openssl}/lib/pkgconfig:${libffi}/lib/pkgconfig"
    export CPPFLAGS="-I${libSystem}/usr/include -I${zlib}/include -I${openssl}/include -I${libffi}/include"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -Qunused-arguments -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
    export CXXFLAGS="$CFLAGS"
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -Wl,-Z -L${libSystem}/usr/lib -L${zlib}/lib -L${openssl}/lib -L${libffi}/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -Wl,-undefined,dynamic_lookup -lSystem"

    export ac_cv_file__dev_ptmx=yes
    export ac_cv_file__dev_ptc=no
    export ac_cv_buggy_getaddrinfo=no
    export ac_cv_little_endian_double=yes
    export ac_cv_working_tzset=yes

    ./configure \
      --host=x86_64-apple-darwin20.4 \
      --build=$(cc -dumpmachine) \
      --prefix=/usr \
      --with-build-python=${python3}/bin/python3 \
      --without-ensurepip \
      --disable-test-modules \
      --disable-framework \
      --disable-shared \
      --with-system-ffi \
      --with-openssl=${openssl} \
      --with-openssl-rpath=no

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    make install DESTDIR="$out"
    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "CPython, cross-built for PureDarwin";
    platforms = platforms.linux;
  };
}
