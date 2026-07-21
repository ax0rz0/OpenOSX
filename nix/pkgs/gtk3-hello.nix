{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, gtk3
, glib
, cairo
, cairoGobject
, pango
, harfbuzz
, freetype2
, fontconfig
, gdkPixbuf
, libepoxy
, atspi2Core
, dbus
}:

let
  # Now that gtk3/glib/cairo/cairo-gobject/pango/gdk-pixbuf/libepoxy/atspi2Core
  # are real dylibs (each absorbing its own static deps - fontconfig,
  # freetype, X11, etc. - at its own build time), the final executable just
  # needs normal dynamic linking. No force_load, no static duplicate-symbol
  # juggling.
  dylibDeps = [ gtk3 glib cairo cairoGobject pango gdkPixbuf libepoxy atspi2Core dbus ];
  headerDeps = dylibDeps ++ [ harfbuzz freetype2 fontconfig ];
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
  pname = "puredarwin-gtk3-hello";
  version = "0.1";
  src = ./gtk3-hello-src;
  dontUnpack = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    CC="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang"
    INSTALL_NAME_TOOL="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-install_name_tool"

    "$CC" \
      -isysroot "$DARWIN_SDK_ROOT" -mmacosx-version-min=11.0 \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -DCAIRO_HAS_GOBJECT_FUNCTIONS=1 \
      -I${libSystem}/usr/include \
      ${lib.concatMapStringsSep " " (dep: "-I${dep}/include") headerDeps} \
      -I${gtk3}/include/gtk-3.0 \
      -I${glib}/include/glib-2.0 -I${glib}/lib/glib-2.0/include \
      -I${cairo}/include/cairo -I${cairoGobject}/include/cairo \
      -I${pango}/include/pango-1.0 \
      -I${harfbuzz}/include/harfbuzz \
      -I${freetype2}/include/freetype2 \
      -I${fontconfig}/include \
      -I${gdkPixbuf}/include/gdk-pixbuf-2.0 \
      -I${atspi2Core}/include/atk-1.0 \
      -I${atspi2Core}/include/at-spi-2.0 \
      -I${atspi2Core}/include/at-spi2-atk/2.0 \
      -I${dbus}/include/dbus-1.0 -I${dbus}/lib/dbus-1.0/include \
      -I${glib}/include/gio-unix-2.0 \
      -isysroot "$DARWIN_SDK_ROOT" -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib \
      ${lib.concatMapStringsSep " " (dep: "-L${dep}/lib") dylibDeps} \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-dylinker_install_name,/usr/lib/dyld \
      -Wl,-platform_version,macos,11.0,11.5 \
      -lgtk-3 -lgdk-3 \
      -lglib-2.0 -lgobject-2.0 -lgio-2.0 -lgmodule-2.0 \
      -lcairo -lcairo-gobject \
      -lpango-1.0 -lpangocairo-1.0 \
      -lgdk_pixbuf-2.0 \
      -lepoxy \
      -latk-1.0 -latk-bridge-2.0 -latspi \
      -ldbus-1 \
      -lSystem \
      $src/hello.c -o gtk3-hello

    # Same @rpath -> absolute-path fix as every other dylib consumer in this
    # project (see dbus.nix); the darwin cross linker embeds @rpath refs for
    # each -l<name> match found via -L, which don't resolve once the dylibs
    # are flattened onto the image root at their real /lib/lib<name>.dylib.
    for dep in ${lib.concatMapStringsSep " " (d: "${d}") dylibDeps}; do
      for so in "$dep"/lib/*.dylib; do
        [ -L "$so" ] && continue
        base=$(basename "$so")
        "$INSTALL_NAME_TOOL" -change "@rpath/$base" "/lib/$base" gtk3-hello 2>/dev/null || true
      done
    done

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    install -m755 gtk3-hello $out/bin/gtk3-hello
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "Minimal GTK3 test window, cross-built for PureDarwin (dynamically linked against the real GTK3/glib/cairo/pango/atspi dylibs)";
    platforms = platforms.linux;
  };
}
