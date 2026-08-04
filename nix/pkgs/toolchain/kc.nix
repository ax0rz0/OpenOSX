{ stdenv
, lib
, kcTools
, kernel
, kexts
}:

let
  kextList = import ../../lib/kc-kexts.nix;
in
stdenv.mkDerivation {
  pname = "puredarwin-kc";
  version = "0.1";

  dontUnpack = true;

  buildPhase = ''
    runHook preBuild
    KERNEL_EXTS=${kernel}/System/Library/Extensions
    KEXTS=${kexts}/System/Library/Extensions

    # The kernel binary's filename depends on which build variant xnu was
    # configured for (RELEASE -> kernel, DEBUG -> kernel.debug, etc.) -
    # pick whichever one is actually present rather than hardcoding it.
    KERNEL_BIN=$(ls "${kernel}"/System/Library/Kernels/kernel* | head -n1)

    codeless=()
    for p in "$KERNEL_EXTS"/System.kext/PlugIns/*.kext; do
      codeless+=( -codeless "$p" )
    done

    ${kcTools}/bin/kc-builder \
      -kernel "$KERNEL_BIN" \
${lib.concatMapStringsSep "\n" (k: "      -kext \"$KEXTS/" + k + "\" \\") kextList}
      "''${codeless[@]}" \
      -o kernel
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp kernel $out/kernel
    runHook postInstall
  '';

  meta = with lib; {
    description = "PureDarwin boot kernel collection (kernel + kexts fileset), assembled by kc-tools' kc-builder";
    platforms = platforms.linux;
  };
}
