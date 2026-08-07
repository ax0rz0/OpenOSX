{ stdenv
, lib
, requireFile
, meson
, ninja
, pkg-config
, bison
, python3
, darwinCrossToolchain
, nativeLd
, libSystem
, libxkbcommon
, libxcb
, libXau
, libXdmcp
, libxml2
, xkeyboard-config
, targetTriple ? "x86_64-apple-darwin20.4"
}:

let
  targetInfo = import ../../lib/target-info.nix targetTriple;

  # libxml2 is libxkbregistry's only dependency; the Wayland driver in Wine
  # will not build without libxkbregistry.
  xDeps = [ libxcb libXau libXdmcp libxml2 ];
  xPkgConfigDeps = map lib.getDev xDeps;
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
  pname = "openosx-libxkbcommon";
  version = libxkbcommon.version;

  src = libxkbcommon.src;

  nativeBuildInputs = [ meson ninja pkg-config bison python3 ];
  buildInputs = xDeps;

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    cat > openosx-cross.ini <<EOF
[binaries]
c = '${darwinCrossToolchain}/bin/${targetTriple}-clang'
ar = '${darwinCrossToolchain}/bin/${targetTriple}-ar'
strip = '${darwinCrossToolchain}/bin/${targetTriple}-strip'
pkgconfig = '${pkg-config}/bin/pkg-config'

[built-in options]
c_args = ['-isysroot', '$DARWIN_SDK_ROOT', '-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=0', '-DHAVE_STRNDUP=1', '-fno-stack-protector', '-I${libSystem}/usr/include']
c_link_args = ['-isysroot', '$DARWIN_SDK_ROOT', '-fuse-ld=${nativeLd}/bin/ld', '-nostdlib', '-L${libSystem}/usr/lib', '-Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib', '-Wl,-dylinker_install_name,/usr/lib/dyld', '-Wl,-platform_version,macos,11.0,11.5', '-lSystem', '${libxcb}/lib/libxcb.a', '${libXau}/lib/libXau.a', '${libXdmcp}/lib/libXdmcp.a', '${libxml2}/lib/libxml2.a']

[host_machine]
system = 'darwin'
cpu_family = '${targetInfo.mesonCpuFamily}'
cpu = '${targetInfo.mesonCpu}'
endian = '${targetInfo.mesonEndian}'
EOF

    export PKG_CONFIG_PATH="${lib.makeSearchPath "lib/pkgconfig" xPkgConfigDeps}:${lib.makeSearchPath "share/pkgconfig" xPkgConfigDeps}"
    meson setup build \
      --cross-file openosx-cross.ini \
      --prefix=$out \
      --buildtype=release \
      -Ddefault_library=static \
      -Dxkb-config-root=/usr/share/X11/xkb \
      -Dx-locale-root=/usr/share/X11/locale \
      -Denable-x11=true \
      -Denable-xkbregistry=true \
      -Denable-wayland=false \
      -Denable-docs=false \
      -Denable-bash-completion=false

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
    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "Real cross-built libxkbcommon (+ libxkbcommon-x11), for i3/keyboard-layout consumers";
    platforms = platforms.linux;
  };
}
