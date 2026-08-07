{ stdenv
, lib
}:

stdenv.mkDerivation {
  pname = "openosx-unifdef-native";
  version = "0.1";

  src = lib.fileset.toSource {
    root = ../../../tools/unifdef;
    fileset = ../../../tools/unifdef/unifdef.c;
  };

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild
    $CC -std=gnu89 -O2 unifdef.c -o unifdef
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp unifdef $out/bin/
    runHook postInstall
  '';

  meta = with lib; {
    description = "Native ELF build of OpenOSX's tools/unifdef, for use as a build-time host tool";
    platforms = platforms.linux;
  };
}
