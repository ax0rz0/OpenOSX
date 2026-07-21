{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, file
, zlib
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
  pname = "puredarwin-file";
  inherit (file) version;
  src = file.src;

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"
    export CC="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang"
    export AR="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ar"
    export RANLIB="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ranlib"
    export STRIP="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-strip"
    export CPPFLAGS="-I${libSystem}/usr/include -I${zlib}/include"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DMAGIC='\"/usr/share/misc/magic\"'"
    # Same trap as toybox: a real Apple SDK's -isysroot makes ld64 implicitly
    # find stub dylibs (libz.1.dylib etc.) that don't exist at runtime here,
    # so disable the implicit search path and force-load our real static zlib.
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -Wl,-Z -L${libSystem}/usr/lib -L${zlib}/lib -Wl,-force_load,${zlib}/lib/libz.a -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -Wl,-undefined,dynamic_lookup -lSystem"

    # Cross-compiling: autoconf can't run test programs, so it guesses
    # conservatively wrong for a few glibc/BSD-only functions file's
    # src/*.c probes for. Force the portable fallbacks.
    for fn in fgetln strlcpy strlcat strcasestr getline mkstemp mkostemp \
              vasprintf asprintf reallocarray funopen pipe2; do
      export "ac_cv_func_''${fn}=no"
    done

    # file bundles its own build-time "compiled magic" generator (a native
    # host binary, not part of the cross build) - autotools handles that via
    # AC_PROG_CC_FOR_BUILD internally, but be explicit so it doesn't try to
    # use our cross clang for it.
    export CC_FOR_BUILD=cc

    ./configure \
      --host=x86_64-apple-darwin20.4 \
      --build=$(cc -dumpmachine) \
      --prefix=$out \
      --disable-shared \
      --enable-static \
      --disable-nls \
      --without-python \
      --disable-libseccomp

    # On Darwin, libm's symbols live in libSystem itself (no separate libm),
    # but file's configure unconditionally adds -lm since it doesn't know
    # that - and -Wl,-Z above means there's no stub libm.dylib for ld64 to
    # silently fall back to anymore. Strip it; -lSystem already provides
    # what's needed (or our libSystem.exports stub does).
    find . -name Makefile -exec sed -i -E 's/-lm\b//g' {} +

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    make install

    # The image builder flattens package contents into the PureDarwin root,
    # so file's compiled-in default must point at a guest path, not $out.
    mkdir -p $out/usr/share/misc
    cp -p $out/share/misc/magic.mgc $out/usr/share/misc/magic.mgc

    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    platforms = platforms.linux;
  };
}
