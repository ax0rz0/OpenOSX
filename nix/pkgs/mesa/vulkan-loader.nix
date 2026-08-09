{ stdenv
, lib
, requireFile
, cmake
, ninja
, python3
, darwinCrossToolchain
, nativeLd
, libSystem
, nativeMesonTools
, pkg-config
, libX11
, libxcb
, libXau
, libXdmcp
, libXrandr
, libXrender
, xorgproto
, vulkanLoader
, vulkanHeaders
, targetTriple ? "x86_64-apple-darwin20.4"
}:

let
  xDeps = [ libX11 libxcb libXau libXdmcp libXrandr libXrender xorgproto ];
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
  pname = "openosx-vulkan-loader";
  inherit (vulkanLoader) version src;

  nativeBuildInputs = [ cmake ninja python3 pkg-config ];

  # elseif(APPLE) fires before the X11 branch and sets only
  # VK_USE_PLATFORM_METAL_EXT, so BUILD_WSI_XCB_SUPPORT is never consulted and
  # the loader ships with no surface extensions at all - vkCreateXcbSurfaceKHR
  # and friends are compiled out. OpenOSX presents X11, not Metal.
  postPatch = ''
    sed -i \
      -e 's/^elseif(APPLE)$/elseif(FALSE)/' \
      -e 's/CMAKE_SYSTEM_NAME MATCHES "Linux|BSD|DragonFly|GNU|CYGWIN"/CMAKE_SYSTEM_NAME MATCHES "Linux|BSD|DragonFly|GNU|CYGWIN|Darwin"/' \
      CMakeLists.txt

    # wsi.c then asserts, via #error, that any __APPLE__ build defines the
    # Metal/MVK platforms. It is a consistency check with no code attached, and
    # the assumption it encodes is the one we are deliberately not making.
    sed -i 's/^#if __APPLE__$/#if 0/' loader/wsi.c
  '';

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PKG_CONFIG_PATH="${lib.concatMapStringsSep ":" (d: "${lib.getDev d}/lib/pkgconfig") xDeps}:${lib.concatMapStringsSep ":" (d: "${lib.getDev d}/share/pkgconfig") xDeps}"
    export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"

    commonFlags="-isysroot $DARWIN_SDK_ROOT -mmacosx-version-min=11.0 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector -I${libSystem}/usr/include ${lib.concatMapStringsSep " " (d: "-I${lib.getDev d}/include") xDeps}"
    linkFlags="-isysroot $DARWIN_SDK_ROOT -mmacosx-version-min=11.0 -fuse-ld=${nativeLd}/bin/ld -nostdlib -L${libSystem}/usr/lib ${lib.concatMapStringsSep " " (d: "-L${d}/lib") xDeps} -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,11.0,11.5 -lSystem"

    cmake -B build -G Ninja \
      -DCMAKE_SYSTEM_NAME=Darwin \
      -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
      -DCMAKE_C_COMPILER=${darwinCrossToolchain}/bin/${targetTriple}-clang \
      -DCMAKE_CXX_COMPILER=${darwinCrossToolchain}/bin/${targetTriple}-clang++ \
      -DCMAKE_AR=${darwinCrossToolchain}/bin/${targetTriple}-ar \
      -DCMAKE_RANLIB=${darwinCrossToolchain}/bin/${targetTriple}-ranlib \
      -DCMAKE_INSTALL_NAME_TOOL=${nativeMesonTools}/bin/install_name_tool \
      -DCMAKE_C_FLAGS="$commonFlags" \
      -DCMAKE_CXX_FLAGS="$commonFlags" \
      -DCMAKE_EXE_LINKER_FLAGS="$linkFlags" \
      -DCMAKE_SHARED_LINKER_FLAGS="$linkFlags" \
      -DCMAKE_INSTALL_PREFIX=$out/usr \
      -DCMAKE_INSTALL_NAME_DIR=/usr/lib \
      -DCMAKE_BUILD_TYPE=Release \
      -DVULKAN_HEADERS_INSTALL_DIR=${vulkanHeaders} \
      -DBUILD_TESTS=OFF \
      -DBUILD_WSI_XCB_SUPPORT=ON \
      -DBUILD_WSI_XLIB_SUPPORT=ON \
      -DBUILD_WSI_WAYLAND_SUPPORT=OFF \
      -DAPPLE_STATIC_LOADER=OFF \
      -DUSE_GAS=OFF \
      -DUSE_MASM=OFF

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    ninja -C build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    ninja -C build install

    INT="${nativeMesonTools}/bin/install_name_tool"
    for dylib in "$out"/usr/lib/*.dylib; do
      [ -L "$dylib" ] && continue
      [ -e "$dylib" ] || continue
      "$INT" -id "/usr/lib/$(basename "$dylib")" "$dylib" 2>/dev/null || true
    done

    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "Vulkan loader, cross-built for OpenOSX (dispatches to the lavapipe ICD)";
    platforms = platforms.linux;
  };
}
