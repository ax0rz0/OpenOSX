{ stdenv
, lib
, requireFile
, pkg-config
, gnumake
, flex
, bison
, darwinCrossToolchain
, nativeLd
, libSystem
, wine
, wineTools
, llvmBintools
, mingwGcc
, libX11
, libxcb
, libXau
, libXdmcp
, libXext
, libXrender
, libXfixes
, libXi
, libXcursor
, libXrandr
, xorgproto
, freetype
, fontconfig
, targetTriple ? "x86_64-apple-darwin20.4"
}:

let
  xDeps = [
    xorgproto libX11 libxcb libXau libXdmcp libXext
    libXrender libXfixes libXi libXcursor libXrandr
  ];
  sdkTarball = requireFile {
    name = "MacOSX11.3.sdk.tar.xz";
    sha256 = "9adc1373d3879e1973d28ad9f17c9051b02931674a3ec2a2498128989ece2cb1";
    message = ''
      MacOSX11.3.sdk.tar.xz (Apple SDK, proprietary - not fetchable/redistributable)
      is not yet in your Nix store. Register your local copy with:
        nix-store --add-fixed sha256 /path/to/MacOSX11.3.sdk.tar.xz
    '';
  };
  # The 11.3 SDK carries AppKit/Metal headers, so Wine's configure would find
  # them and enable winemac.drv against frameworks PureDarwin does not have.
  # Pre-seed the header caches to no so the X11 driver is chosen instead.
  macFrameworkOverrides = lib.concatStringsSep " " [
    "ac_cv_header_AppKit_AppKit_h=no"
    "ac_cv_header_Metal_Metal_h=no"
    "ac_cv_header_CoreGraphics_CoreGraphics_h=no"
    "ac_cv_header_ApplicationServices_ApplicationServices_h=no"
    "ac_cv_header_Carbon_Carbon_h=no"
    "ac_cv_header_QuartzCore_QuartzCore_h=no"
    "ac_cv_header_CoreAudio_CoreAudio_h=no"
    "ac_cv_header_AudioToolbox_AudioToolbox_h=no"
    "ac_cv_header_AudioUnit_AudioUnit_h=no"
    "ac_cv_header_IOKit_IOKit_h=no"
    "ac_cv_header_Security_Security_h=no"
    "ac_cv_header_OpenAL_al_h=no"
  ];
in
stdenv.mkDerivation {
  pname = "puredarwin-wine-configure-probe";
  inherit (wine) version;
  src = wine.src;

  # mingwGcc builds Wine's PE-format modules. Like winebuild it runs on the
  # build host and emits Windows binaries, so it never touches PureDarwin.
  nativeBuildInputs = [ pkg-config gnumake flex bison mingwGcc ];
  buildInputs = xDeps ++ [ freetype fontconfig ];

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    # Wine probes for target libraries by running otool -L on them, so without
    # otool on PATH every single -l<lib> check reports "not found" even when the
    # library is present. It has to be a build-host binary that can read Mach-O:
    # PureDarwin's own cctools otool is itself Mach-O and will not execute here.
    mkdir -p host-tools
    ln -sf ${llvmBintools}/bin/llvm-otool host-tools/otool
    export PATH="$PWD/host-tools:${darwinCrossToolchain}/bin:$PATH"
    export PKG_CONFIG_PATH="${lib.makeSearchPath "lib/pkgconfig" (map lib.getDev (xDeps ++ [ freetype fontconfig ]))}"
    export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
    export CC="${darwinCrossToolchain}/bin/${targetTriple}-clang"
    export CXX="${darwinCrossToolchain}/bin/${targetTriple}-clang++"
    export AR="${darwinCrossToolchain}/bin/${targetTriple}-ar"
    export RANLIB="${darwinCrossToolchain}/bin/${targetTriple}-ranlib"
    export STRIP="${darwinCrossToolchain}/bin/${targetTriple}-strip"
    export CPPFLAGS="-I${libSystem}/usr/include ${lib.concatMapStringsSep " " (dep: "-I${lib.getDev dep}/include") xDeps}"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector"
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -L${libSystem}/usr/lib ${lib.concatMapStringsSep " " (dep: "-L${dep}/lib") xDeps} -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -lSystem"

    # --without-freetype is temporary: freetype is still a static-only build, and
    # Wine's check needs a dylib. Skipping it keeps the probe moving so the rest
    # of configure still gets exercised; drop the flag once freetype ships one.
    # Keep going past a configure failure: the log is the deliverable.
    set +e
    env ${macFrameworkOverrides} ./configure \
      --host=${targetTriple} \
      --build=$(cc -dumpmachine) \
      --prefix=/ \
      --with-wine-tools=${wineTools} \
      --enable-win64 \
      --with-x \
      --with-mingw \
      --without-freetype \
      --without-alsa \
      --without-capi \
      --without-cups \
      --without-dbus \
      --without-gnutls \
      --without-gssapi \
      --without-gstreamer \
      --without-krb5 \
      --without-netapi \
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
      --without-wayland \
      2>&1 | tee configure-output.log
    configureStatus=''${PIPESTATUS[0]}
    set -e
    echo "configure exit status: $configureStatus" >> configure-output.log

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    set +e
    make -j$NIX_BUILD_CORES dlls/ntdll/ntdll.so 2>&1 | tail -n 400 > ntdll-build.log
    echo "ntdll make exit status: ''${PIPESTATUS[0]}" >> ntdll-build.log
    set -e
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$out"
    cp configure-output.log "$out/" || true
    cp ntdll-build.log "$out/" || true
    cp config.log "$out/" || true
    cp include/config.h "$out/config.h" || true
    runHook postInstall
  '';

  dontFixup = true;
}
