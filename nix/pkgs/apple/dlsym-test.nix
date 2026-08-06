{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, targetTriple ? "x86_64-apple-darwin20.4"
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
  cc = "${darwinCrossToolchain}/bin/${targetTriple}-clang";
in
stdenv.mkDerivation {
  pname = "openosx-dlsym-test";
  version = "1";
  dontUnpack = true;

  buildPhase = ''
    runHook preBuild
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    cat > dlsym-test.c <<'EOF'
    #include <dlfcn.h>
    #include <stdio.h>

    int main(int argc, char **argv) {
        const char *path = argc > 1 ? argv[1]
            : "/usr/lib/wine/x86_64-unix/ws2_32.so";
        const char *sym  = argc > 2 ? argv[2] : "__wine_unix_call_funcs";

        printf("dlopen(%s, RTLD_NOW)\n", path); fflush(stdout);
        void *h = dlopen(path, RTLD_NOW);
        printf("  handle = %p\n", h); fflush(stdout);
        if (!h) { printf("  dlerror: %s\n", dlerror()); return 1; }

        printf("dlsym(handle, \"%s\")\n", sym); fflush(stdout);
        dlerror();
        void *p = dlsym(h, sym);
        const char *e = dlerror();
        printf("  addr = %p\n", p);
        printf("  dlerror: %s\n", e ? e : "(none)"); fflush(stdout);

        /* RTLD_DEFAULT too: distinguishes "handle scope broken" from
         * "symbol not visible to dlsym at all". */
        dlerror();
        void *g = dlsym(RTLD_DEFAULT, sym);
        printf("dlsym(RTLD_DEFAULT) = %p (%s)\n", g, dlerror() ?: "(none)");
        fflush(stdout);

        return p ? 0 : 1;
    }
    EOF

    ${cc} -isysroot "$DARWIN_SDK_ROOT" -I${libSystem}/usr/include \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib -L${libSystem}/usr/lib \
      -Wl,-platform_version,macos,11.0,11.5 -Wl,-fixup_chains \
      -Wl,-rpath,/usr/lib/wine/x86_64-unix \
      -lSystem -o dlsym-test dlsym-test.c
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/usr/bin
    cp dlsym-test $out/usr/bin/
    runHook postInstall
  '';

  dontFixup = true;
  meta = with lib; {
    description = "dlopen/dlsym smoke test (/usr/bin/dlsym-test [path] [symbol])";
    platforms = platforms.linux;
  };
}
