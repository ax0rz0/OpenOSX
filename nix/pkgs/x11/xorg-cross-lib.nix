{ stdenv
, lib
, requireFile
, pkg-config
, gnumake
, perl
, darwinCrossToolchain
, nativeLd
, libSystem
, src
, pname
, version
, deps ? []
, nativeDeps ? []
, configureFlags ? []
, preConfigureExtra ? ""
, postPatchExtra ? ""
, postInstallExtra ? ""
, patches ? []
, targetTriple ? "x86_64-apple-darwin20.4"
, guestPrefix ? false
, shared ? false
, extraLinkFlags ? [ ]
, nativeMesonTools ? null
}:

assert shared -> nativeMesonTools != null;

let
  depPcPaths = map lib.getDev (deps ++ nativeDeps);
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
  inherit pname version src patches;

  postPatch = postPatchExtra;

  nativeBuildInputs = [
    pkg-config
    gnumake
    perl
  ] ++ nativeDeps;

  buildInputs = deps;

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"
    export PKG_CONFIG_PATH="${lib.makeSearchPath "lib/pkgconfig" depPcPaths}:${lib.makeSearchPath "share/pkgconfig" depPcPaths}"
    export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
    export CC="${darwinCrossToolchain}/bin/${targetTriple}-clang"
    export AR="${darwinCrossToolchain}/bin/${targetTriple}-ar"
    export RANLIB="${darwinCrossToolchain}/bin/${targetTriple}-ranlib"
    export NM="${darwinCrossToolchain}/bin/${targetTriple}-nm"
    export LD="${nativeLd}/bin/ld"
    # -dylinker_install_name is omitted when building dylibs
    export STRIP="${darwinCrossToolchain}/bin/${targetTriple}-strip"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector -I${libSystem}/usr/include ${lib.concatMapStringsSep " " (dep: "-I${lib.getDev dep}/include") deps}"
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -L${libSystem}/usr/lib ${lib.concatMapStringsSep " " (dep: "-L${dep}/lib") deps} -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib${lib.optionalString (!shared) " -Wl,-dylinker_install_name,/usr/lib/dyld"} -Wl,-platform_version,macos,11.0,11.5 -lSystem"

    ${preConfigureExtra}

    ./configure \
      --host=${targetTriple} \
      --build=$(cc -dumpmachine) \
      --prefix=${if guestPrefix then "/" else "$out"} \
      ${if shared then "--enable-shared --disable-static" else "--disable-shared --enable-static"} \
      ${lib.escapeShellArgs configureFlags}

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    # extraLinkFlags is applied here rather than in LDFLAGS at configure time:
    # -force_load pulls a whole archive in, and configure's "can the compiler
    # create executables" test links a trivial program with no other libraries,
    # so the archive's own undefined symbols make that probe fail.
    make -j$NIX_BUILD_CORES${lib.optionalString (extraLinkFlags != [ ])
      " LDFLAGS=\"$LDFLAGS ${lib.concatStringsSep " " extraLinkFlags}\""}
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    make install${lib.optionalString guestPrefix " DESTDIR=$out"}
    ${lib.optionalString guestPrefix ''
      # .pc files inherit prefix=/ from configure, which is correct for the guest
      # but unusable by anything cross-compiling against this package here. Point
      # them back into the store; only build-time consumers ever read them.
      for pc in "$out"/lib/pkgconfig/*.pc "$out"/share/pkgconfig/*.pc; do
        [ -f "$pc" ] || continue
        substituteInPlace "$pc" --replace-quiet "prefix=/" "prefix=$out"
      done

      # Drop the libtool archives. Under this prefix they record libdir='//lib'
      # and cross-reference other packages' '//lib/*.la', which resolve to nothing
      # at build time and cannot be rewritten - each path belongs to a different
      # store output. Everything here is a static library shipping a .pc, so
      # libtool consumers fall back to -L/-l from pkg-config, which is correct.
      rm -f "$out"/lib/*.la
    ''}
    ${lib.optionalString shared ''
      INSTALL_NAME_TOOL="${nativeMesonTools}/bin/install_name_tool"
      dylibs=$(find "$out/lib" -maxdepth 1 -name "*.dylib" -not -type l)
      for dylib in $dylibs; do
        base=$(basename "$dylib")
        "$INSTALL_NAME_TOOL" -id "/lib/$base" "$dylib"
      done
      allfiles=$(
        [ ! -d "$out/bin" ] || find "$out/bin" -type f
        [ ! -d "$out/lib" ] || find "$out/lib" -type f
      )
      for f in $allfiles; do
        for dylib in $dylibs; do
          base=$(basename "$dylib")
          "$INSTALL_NAME_TOOL" -change "@rpath/$base" "/lib/$base" "$f" 2>/dev/null || true
          # a sibling inside the same project can be recorded by absolute install
          # path rather than @rpath (libxfce4windowingui -> libxfce4windowing), which
          # the @rpath rewrite above never matches
          "$INSTALL_NAME_TOOL" -change "$out/lib/$base" "/lib/$base" "$f" 2>/dev/null || true
          # siblings inside one project can be recorded by absolute install path
          # rather than @rpath (libxfce4windowingui -> libxfce4windowing), which the
          # @rpath rewrite above never matches
          "$INSTALL_NAME_TOOL" -change "$out/lib/$base" "/lib/$base" "$f" 2>/dev/null || true
        done
      done
    ''}
    ${postInstallExtra}
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    platforms = platforms.linux;
  };
}
