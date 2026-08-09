{ stdenv
, lib
, requireFile
, pkg-config
, gnumake
, autoreconfHook
, utilmacros
, darwinCrossToolchain
, nativeLd
, libSystem
, xdpyinfo
, libX11
, libxcb
, libXau
, libXdmcp
, xorgproto
, targetTriple ? "x86_64-apple-darwin20.4"
}:

# xdpyinfo, cross-compiled for the guest.
#
# Prints what the X server actually believes about the display: screen count
# and geometry, depths, visuals, and the extension list. On OpenOSX that is the
# quickest way to tell whether the GOP framebuffer came up at the resolution
# IOGOPFramebuffer reported, or whether Xorg fell back to something else.
#
# Deliberately built against the X11 core alone. xdpyinfo's optional sections
# (Xinerama, XInput, Xrender, XTest, DMX, XF86VidMode, Composite) are each
# gated on their own pkg-config check with an action-if-not-found, so leaving
# those libraries out simply omits those sections rather than failing the
# configure - and none of them are informative on a single software-rendered
# framebuffer.

let
  xDeps = [ xorgproto libX11 libxcb libXau libXdmcp ];
  xForceLoad = lib.concatStringsSep " " [
    "-Wl,-force_load,${libX11}/lib/libX11.a"
    "-Wl,-force_load,${libxcb}/lib/libxcb.a"
    "-Wl,-force_load,${libXau}/lib/libXau.a"
    "-Wl,-force_load,${libXdmcp}/lib/libXdmcp.a"
  ];
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
  pname = "openosx-xdpyinfo";
  inherit (xdpyinfo) version;
  src = xdpyinfo.src;

  # Unlike xprop, nixpkgs carries xdpyinfo as a git checkout rather than a
  # release tarball, so there is no generated ./configure to run.
  # autoreconfHook makes one; utilmacros supplies the XORG_* m4 that xorg's
  # configure.ac expects to find via ACLOCAL_PATH.
  nativeBuildInputs = [
    pkg-config
    gnumake
    autoreconfHook
    utilmacros
  ];

  buildInputs = xDeps;

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"
    export PKG_CONFIG_PATH="${lib.makeSearchPath "lib/pkgconfig" (map lib.getDev xDeps)}:${lib.makeSearchPath "share/pkgconfig" (map lib.getDev xDeps)}"
    export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
    export CC="${darwinCrossToolchain}/bin/${targetTriple}-clang"
    export AR="${darwinCrossToolchain}/bin/${targetTriple}-ar"
    export RANLIB="${darwinCrossToolchain}/bin/${targetTriple}-ranlib"
    export STRIP="${darwinCrossToolchain}/bin/${targetTriple}-strip"
    export CPPFLAGS="-I${libSystem}/usr/include ${lib.concatMapStringsSep " " (dep: "-I${lib.getDev dep}/include") xDeps}"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector -DNO_XPOLL_H"
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -L${libSystem}/usr/lib ${lib.concatMapStringsSep " " (dep: "-L${dep}/lib") xDeps} -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -Wl,-undefined,dynamic_lookup -lSystem"
    export LIBS="${xForceLoad} -lSystem"

    ./configure \
      --host=${targetTriple} \
      --build=$(cc -dumpmachine) \
      --prefix=$out

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
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    platforms = platforms.linux;
  };
}
