{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, toybox
, gnumake
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
  pname = "puredarwin-toybox";
  inherit (toybox) version;
  src = toybox.src;

  nativeBuildInputs = [ gnumake ];

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"
    export CC="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang"
    export HOSTCC=cc
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -I${libSystem}/usr/include"
    # scripts/make.sh test-links "-l$i" for a fixed list of optional libs
    # (util, resolv, z, iconv, ...) and keeps whichever succeed. Without
    # -Wl,-Z, ld64 implicitly searches $DARWIN_SDK_ROOT/usr/lib (a REAL Apple
    # SDK, which ships stub dylibs for all of those) even though we passed
    # -nostdlib, so every one of those probes "succeeds" against a dylib that
    # doesn't exist on this system's runtime - -Wl,-Z disables that implicit
    # search path so only our explicit -L dirs are considered.
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -Wl,-Z -L${libSystem}/usr/lib -L${zlib}/lib -Wl,-force_load,${zlib}/lib/libz.a -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -Wl,-undefined,dynamic_lookup -lSystem"
    # scripts/portability.sh picks LDOPTIMIZE by checking `uname` of the
    # *build* host, not the target - on our Linux build host it always picks
    # the GNU-ld-only branch (--gc-sections/--as-needed), which our ld64-style
    # nativeLd rejects, even though we're targeting Darwin. Its `:=` only
    # skips the default when the var is already non-empty (not just set), so
    # pre-set it to the same dead_strip flag the Darwin branch would have used.
    export LDOPTIMIZE="-Wl,-dead_strip"

    # The source tarball doesn't preserve the executable bit on its shell
    # scripts, and our custom configurePhase runs before Nix's normal
    # patchShebangs fixup phase would have run - "make defconfig" invokes
    # scripts/genconfig.sh directly (not via `sh scripts/...`), so it needs
    # both +x and a resolvable shebang before that point.
    chmod +x scripts/*.sh
    patchShebangs scripts/

    # defconfig (everything toybox supports) pulls in Linux-specific applets
    # like mount/losetup/insmod that use Linux-only syscalls/headers
    # (MS_NOATIME etc.) with no Darwin equivalent, and we don't need them -
    # this system has its own VNOP-based mount elsewhere. Build only the
    # applets that replace the busybox commands image.nix actually wires up.
    make allnoconfig
    # toybox has no scripts/config helper (unlike the Linux kernel); .config
    # is a plain KEY=y/KEY is not set text file, so flip the options directly.
    for opt in \
      AWK BASENAME CAT CHMOD CLEAR CMP CP CUT DATE DD DF DIFF DIRNAME DU \
      ECHO ENV EXPR FALSE FIND GREP GUNZIP GZIP HEAD HOSTNAME ID KILL LN \
      LS MKDIR MKNOD MORE MV NETCAT PATCH PRINTF PWD READLINK REALPATH \
      RESET RM RMDIR SED SEQ SH SLEEP SORT STAT \
      TAIL TAR TEST TOUCH TR TRUE \
      TRUNCATE TTY UNAME UNIQ VI WC WHICH WHOAMI XARGS YES; do
      if LC_ALL=C grep -q "^CONFIG_''${opt}=" .config 2>/dev/null; then
        sed -i "s/^CONFIG_''${opt}=.*/CONFIG_''${opt}=y/" .config
      elif LC_ALL=C grep -q "^# CONFIG_''${opt} is not set" .config 2>/dev/null; then
        sed -i "s/^# CONFIG_''${opt} is not set/CONFIG_''${opt}=y/" .config
      else
        echo "CONFIG_''${opt}=y" >> .config
      fi
    done
    # No `make oldconfig` needed: allnoconfig + the direct edits above
    # already give every option a definitive value, so there's nothing left
    # to prompt for (and `yes | make oldconfig` fails under pipefail once
    # oldconfig exits and `yes` gets SIGPIPE on its now-closed stdout).

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES toybox
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp toybox $out/bin/toybox
    LC_ALL=C grep -oE '^CONFIG_[A-Z0-9_]+=y$' .config \
      | sed -e 's/^CONFIG_//' -e 's/=y$//' \
      | tr 'A-Z' 'a-z' > $out/applets.txt
    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    platforms = platforms.linux;
  };
}
