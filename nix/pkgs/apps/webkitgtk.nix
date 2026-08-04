{ stdenv
, lib
, requireFile
, cmake
, ninja
, pkg-config
, python3
, perl
, ruby
, gperf
, unifdef
, glibNative
, nativeMesonTools
, darwinCrossToolchain
, nativeLd
, libSystem
, libcxxDylib
, libcxxabiDylib
, webkitgtk
, deps
, icu
, targetTriple ? "x86_64-apple-darwin20.4"
# Requires C++23, needs a port and a half
, configureOnly ? true
}:

let
  depPcPaths = map lib.getDev deps;
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
  pname = "puredarwin-webkitgtk";
  inherit (webkitgtk) version src;

  nativeBuildInputs = [
    cmake ninja pkg-config python3 perl ruby gperf unifdef glibNative
  ];
  buildInputs = deps;

  postPatch = ''
    patchShebangs .

    # CMake sets APPLE from CMAKE_SYSTEM_NAME=Darwin, and WebKit's build files
    # use APPLE to mean "the Cocoa port" - so a GTK build on Darwin tries to
    # compile Cocoa sources the GTK tarball does not even ship
    # (darwin/OSLogPrintStream.mm, cf/*, cocoa/*) and to pass Cocoa link flags
    # (-sub_library libobjc -umbrella WebKit). Upstream already distinguishes
    # the two elsewhere with a PORT STREQUAL "Mac" check; this makes the
    # port-specific sites use that instead of APPLE. The C++ side needs no such
    # patch - it is written for OS(DARWIN) independently of PLATFORM().
    patch -p1 < ${./webkitgtk-gtk-port-on-darwin.patch}
  '';

  configurePhase = ''
    runHook preConfigure
    export PATH="${nativeMesonTools}/bin:${glibNative}/bin:$PATH"

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PKG_CONFIG_PATH="${lib.makeSearchPath "lib/pkgconfig" depPcPaths}:${lib.makeSearchPath "share/pkgconfig" depPcPaths}:${lib.makeSearchPath "usr/lib/pkgconfig" deps}"
    export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"

    commonFlags="-isysroot $DARWIN_SDK_ROOT -mmacosx-version-min=11.0 -Qunused-arguments -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector -I${libSystem}/usr/include ${lib.concatMapStringsSep " " (dep: "-I${lib.getDev dep}/include") deps}"
    linkFlags="-isysroot $DARWIN_SDK_ROOT -mmacosx-version-min=11.0 -fuse-ld=${nativeLd}/bin/ld -nostdlib -L${libSystem}/usr/lib -L${libcxxDylib}/usr/lib -L${libcxxabiDylib}/usr/lib ${lib.concatMapStringsSep " " (dep: "-L${dep}/lib") deps} -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,11.0,11.5 -lc++ -lc++abi -lSystem"

    # USE_APPLE_ICU defaults to CMake's APPLE variable, i.e. ON here, which
    # sends ICU detection down the Mac port's path: a single libicucore, with
    # headers expected in an Apple-internal staging dir. That branch also
    # builds the ICU::data target from an unchecked find_library result, so a
    # miss reaches the link line as the literal string ICU_DATA_LIBRARY-NOTFOUND
    # instead of failing configure. Off gets CMake's normal FindICU.
    cmake -B build -G Ninja \
      -DCMAKE_SYSTEM_NAME=Darwin \
      -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
      -DCMAKE_C_COMPILER=${darwinCrossToolchain}/bin/${targetTriple}-clang \
      -DCMAKE_CXX_COMPILER=${darwinCrossToolchain}/bin/${targetTriple}-clang++ \
      -DCMAKE_AR=${darwinCrossToolchain}/bin/${targetTriple}-ar \
      -DCMAKE_RANLIB=${darwinCrossToolchain}/bin/${targetTriple}-ranlib \
      -DCMAKE_INSTALL_NAME_TOOL=${nativeMesonTools}/bin/install_name_tool \
      -DCMAKE_C_FLAGS="$commonFlags" \
      -DCMAKE_CXX_FLAGS="$commonFlags -nostdinc++ -I${libcxxDylib}/usr/include/c++/v1" \
      -DCMAKE_EXE_LINKER_FLAGS="$linkFlags" \
      -DCMAKE_SHARED_LINKER_FLAGS="$linkFlags" \
      -DCMAKE_INSTALL_PREFIX=$out \
      -DCMAKE_INSTALL_NAME_DIR=/lib \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CROSSCOMPILING=ON \
      -DCMAKE_FIND_ROOT_PATH="${lib.concatStringsSep ";" (lib.concatMap (d: [ "${d}" "${d}/usr" ]) deps)}" \
      -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
      -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
      -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
      -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
      -DPORT=GTK \
      -DUSE_APPLE_ICU=OFF \
      -DUSE_GTK4=OFF \
      -DENABLE_INTROSPECTION=OFF \
      -DENABLE_JOURNALD_LOG=OFF \
      -DENABLE_DOCUMENTATION=OFF \
      -DENABLE_BUBBLEWRAP_SANDBOX=OFF \
      -DUSE_GSTREAMER=OFF \
      -DENABLE_VIDEO=OFF \
      -DENABLE_WEB_AUDIO=OFF \
      -DENABLE_WEB_CODECS=OFF \
      -DENABLE_SPEECH_SYNTHESIS=OFF \
      -DENABLE_MEDIA_STREAM=OFF \
      -DENABLE_WEB_RTC=OFF \
      -DENABLE_GAMEPAD=OFF \
      -DENABLE_SPELLCHECK=OFF \
      -DENABLE_WEBGL=OFF \
      -DENABLE_XSLT=OFF \
      -DENABLE_GPU_PROCESS=OFF \
      -DENABLE_WEBXR=OFF \
      -DENABLE_WK_WEB_EXTENSIONS=OFF \
      -DUSE_AVIF=OFF \
      -DUSE_JPEGXL=OFF \
      -DUSE_LCMS=OFF \
      -DUSE_WOFF2=OFF \
      -DUSE_GBM=OFF \
      -DUSE_LIBDRM=OFF \
      -DUSE_LIBBACKTRACE=OFF \
      -DUSE_LIBSECRET=OFF \
      -DUSE_LIBHYPHEN=OFF \
      -DUSE_LIBRICE=OFF \
      -DUSE_SYSPROF_CAPTURE=OFF \
      -DENABLE_API_TESTS=OFF \
      -DENABLE_LAYOUT_TESTS=OFF \
      -DENABLE_MINIBROWSER=OFF

    runHook postConfigure
  '';

  dontBuild = configureOnly;

  buildPhase = lib.optionalString (!configureOnly) ''
    runHook preBuild
    ninja -C build
    runHook postBuild
  '';

  installPhase = if configureOnly then ''
    runHook preInstall
    mkdir -p $out/share/webkit-configure
    cp configure-output.log $out/share/webkit-configure/
    cp build/CMakeCache.txt $out/share/webkit-configure/ 2>/dev/null || true
    runHook postInstall
  '' else ''
    runHook preInstall
    ninja -C build install
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "WebKitGTK for PureDarwin - configure probe (stage 2 of the port)";
    platforms = platforms.linux;
  };
}
