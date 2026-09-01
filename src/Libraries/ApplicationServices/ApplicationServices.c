/*
 * ApplicationServices.framework - the umbrella, and nothing else.
 *
 * On macOS this is a wrapper that re-exports CoreGraphics, CoreText, ImageIO,
 * ATS and friends. Like Cocoa.framework, it contains no code of its own, and
 * binaries link it far more often than they use it.
 *
 * openjdk 21's `java` is the case that motivated this. Measured with
 * tools/compat/machoscan.py, it links six things:
 *
 *     /usr/lib/libz.1.dylib
 *     @rpath/libjli.dylib
 *     /System/Library/Frameworks/Cocoa.framework/.../Cocoa
 *     /System/Library/Frameworks/Security.framework/.../Security
 *     /System/Library/Frameworks/ApplicationServices.framework/.../ApplicationServices
 *     /usr/lib/libSystem.B.dylib
 *
 * ...and imports symbols from exactly two of them: libjli (9, and it ships in
 * the JDK bundle) and libSystem (2). Cocoa, Security, ApplicationServices and
 * libz contribute nothing but a load-time existence check. Three of the four
 * already exist here; this is the fourth.
 *
 * Re-exports nothing, for the same reason Cocoa does not: an LC_REEXPORT_DYLIB
 * naming a dylib that is absent fails at load, which is worse than the gap it
 * would paper over. CoreGraphics does exist and is deliberately still left out,
 * so that a binary resolving a CG symbol does it directly rather than through a
 * path that will change meaning when this umbrella grows up.
 */

const char OpenOSXApplicationServicesVersionString[] =
    "OpenOSX ApplicationServices umbrella 0.1";
