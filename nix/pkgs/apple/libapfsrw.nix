{ stdenv
, lib
, cmake
, ninja
}:

stdenv.mkDerivation {
  pname = "libapfsrw";
  version = "0.1";

  src = ../../../projects/libapfsrw;

  nativeBuildInputs = [ cmake ninja ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
  ];

  meta = with lib; {
    description = "OpenOSX userspace APFS image read/write library and tool";
    platforms = platforms.unix;
  };
}
