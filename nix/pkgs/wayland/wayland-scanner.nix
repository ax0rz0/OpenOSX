{ stdenv
, lib
, meson
, ninja
, pkg-config
, expat
, libxml2
, src
}:

stdenv.mkDerivation {
  pname = "openosx-wayland-scanner-native";
  version = "1.25.0";
  inherit src;

  nativeBuildInputs = [ meson ninja pkg-config ];
  buildInputs = [ expat libxml2 ];

  mesonFlags = [
    "-Dlibraries=false"
    "-Dscanner=true"
    "-Dtests=false"
    "-Ddocumentation=false"
    "-Ddtd_validation=false"
  ];

  meta = with lib; {
    description = "Native Wayland protocol scanner used during OpenOSX cross builds";
    platforms = platforms.linux;
  };
}
