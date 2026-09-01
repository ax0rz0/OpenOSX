{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, libobjc
, corefoundation
, src
, targetTriple ? "x86_64-apple-darwin20.4"
}:

# Cross-builds the small real slice of Foundation vendored at
# src/Libraries/Foundation into /usr/lib/libFoundation.dylib: NSString /
# NSCFString (the CFString toll-free bridge target registered by
# NSCFString.m) and NSAttributedString's minimal declaration. NSObject
# itself lives in libobjc (objc4's runtime/NSObject.mm), not here - matches
# real Darwin, where NSObject is the objc runtime's root class.

let
  sdkTarball = requireFile {
    name = "MacOSX11.3.sdk.tar.xz";
    sha256 = "cd4f08a75577145b8f05245a2975f7c81401d75e9535dcffbb879ee1deefcbf4";
    message = ''
      MacOSX11.3.sdk.tar.xz (Apple SDK, proprietary - not fetchable/redistributable)
      is not yet in your Nix store. Register your local copy with:
        nix-store --add-fixed sha256 /path/to/MacOSX11.3.sdk.tar.xz
    '';
  };

  cc = "${darwinCrossToolchain}/bin/${targetTriple}-clang";
  mmSrcs = [
    "String.subproj/NSString"
    "String.subproj/NSCFString"
    "Collections.subproj/NSArray"
    "Collections.subproj/NSDictionary"
    "URL.subproj/NSURL"
    "Runtime.subproj/NSError"
    # Everything openjdk's libjli.dylib needs, and nothing more. Measured: it
    # references two Foundation classes and sends five selectors -
    # blockOperationWithBlock:, drain, init, start, and
    # performSelectorOnMainThread:withObject:waitUntilDone:
    "Runtime.subproj/NSAutoreleasePool"
    "Runtime.subproj/NSObjectMainThread"
    "Operation.subproj/NSBlockOperation"
  ];
in
stdenv.mkDerivation {
  pname = "openosx-foundation";
  version = "1";

  inherit src;

  nativeBuildInputs = [ stdenv.cc ];

  buildPhase = ''
    runHook preBuild

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    # Stage a "Foundation/" include dir (same trick corefoundation.nix uses)
    # so `#import <Foundation/NSString.h>` etc. resolve like a real
    # framework search path would. Scoped to Runtime.subproj/String.subproj
    # only (not `.` broadly) - the SDK tarball just got extracted alongside
    # these sources and its own real Foundation.framework/*.h must not be
    # picked up here.
    mkdir -p foundation-headers/Foundation
    find Runtime.subproj String.subproj Collections.subproj URL.subproj Operation.subproj -name '*.h' -exec cp {} foundation-headers/Foundation/ \;

    # corefoundation.nix installs its headers flattened into $out/include
    # (no "CoreFoundation/" subdirectory) - stage the same
    # "<CoreFoundation/Foo.h>" layout our sources expect.
    mkdir -p cf-headers
    ln -s ${corefoundation}/include cf-headers/CoreFoundation

    CFLAGS="-x objective-c -fno-objc-arc -fPIC -Os -DNDEBUG -D__OPENOSX__=1 \
      -DDEPLOYMENT_RUNTIME_OBJC=1 -DINCLUDE_OBJC=1 \
      -isysroot $DARWIN_SDK_ROOT \
      -Ifoundation-headers \
      -Icf-headers \
      -I${libSystem}/usr/include \
      -I${libobjc}/usr/include \
      -I${corefoundation}/include"

    objs=""
    for s in ${lib.concatStringsSep " " mmSrcs}; do
      objfile="$(basename $s).o"
      ${cc} $CFLAGS -c "$s.m" -o "$objfile"
      objs="$objs $objfile"
    done

    ${cc} -isysroot "$DARWIN_SDK_ROOT" -dynamiclib \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libobjc}/usr/lib -L${corefoundation}/usr/lib \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-install_name,/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
      -Wl,-fixup_chains \
      -lobjc -lCoreFoundation -lSystem \
      -o libFoundation.dylib $objs

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/usr/lib $out/usr/include/Foundation
    cp libFoundation.dylib $out/usr/lib/

    # Also install the framework layout, because that is the path binaries
    # actually ask dyld for:
    #   /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
    #
    # Note the C. Foundation is one of the frameworks that never moved off its
    # historical version letter - it is Versions/C where almost everything else
    # is Versions/A - and a binary asking for C does not fall back to A.
    #
    # openjdk's libjli.dylib is what surfaced this: Foundation was built and
    # staged, but only as /usr/lib/libFoundation.dylib, so the framework path
    # did not exist and java failed at load with "image not found".
    frameworkDir="$out/System/Library/Frameworks/Foundation.framework"
    mkdir -p "$frameworkDir/Versions/C"
    cp libFoundation.dylib "$frameworkDir/Versions/C/Foundation"
    ln -s C "$frameworkDir/Versions/Current"
    ln -s Versions/Current/Foundation "$frameworkDir/Foundation"
    mkdir -p "$frameworkDir/Versions/C/Headers"
    find Runtime.subproj String.subproj Collections.subproj URL.subproj Operation.subproj -name '*.h'       -exec cp {} "$frameworkDir/Versions/C/Headers/" \;
    ln -s Versions/Current/Headers "$frameworkDir/Headers"
    find Runtime.subproj String.subproj Collections.subproj URL.subproj Operation.subproj -name '*.h' -exec cp {} $out/usr/include/Foundation/ \;
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "OpenOSX Foundation (NSString/NSCFString real toll-free bridge slice), cross-built as /usr/lib/libFoundation.dylib";
    platforms = platforms.linux;
  };
}
