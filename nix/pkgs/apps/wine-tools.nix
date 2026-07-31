{ stdenv
, lib
, pkg-config
, gnumake
, flex
, bison
, wine
}:

stdenv.mkDerivation {
  pname = "wine-native-tools";
  inherit (wine) version;
  src = wine.src;

  nativeBuildInputs = [ pkg-config gnumake flex bison ];

  configurePhase = ''
    runHook preConfigure

    ./configure \
      --prefix="$out" \
      --enable-win64 \
      --without-x \
      --without-freetype \
      --without-mingw \
      --without-alsa \
      --without-capi \
      --without-cups \
      --without-dbus \
      --without-fontconfig \
      --without-gnutls \
      --without-gssapi \
      --without-gstreamer \
      --without-krb5 \
      --without-netapi \
      --without-opencl \
      --without-oss \
      --without-pcap \
      --without-pulse \
      --without-sane \
      --without-sdl \
      --without-udev \
      --without-unwind \
      --without-usb \
      --without-v4l2 \
      --without-vulkan \
      --without-wayland

    runHook postConfigure
  '';

  # __tooldeps__ builds just the tools, not the whole of Wine.
  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES __tooldeps__
    runHook postBuild
  '';

  # The cross build wants the build tree itself (it looks for tools/winebuild
  # relative to what --with-wine-tools names), so keep the layout intact.
  installPhase = ''
    runHook preInstall
    mkdir -p "$out"
    cp -r . "$out/"
    runHook postInstall
  '';

  dontFixup = true;
}
