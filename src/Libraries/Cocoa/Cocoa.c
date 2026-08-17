/*
 * Cocoa.framework - the umbrella, and nothing else.
 *
 * On macOS Cocoa.framework contains no code of its own. It is a wrapper whose
 * only job is to re-export AppKit, Foundation and CoreData, so that linking one
 * framework pulls in all three. Enormous numbers of binaries link it out of
 * habit or build-system default and then never touch a single symbol from it.
 *
 * Measured against the corpus of real macOS binaries: 43 link Cocoa or AppKit,
 * and 33 of those import ZERO NS* symbols. That set is the entire JDK
 * toolchain - java, javac, jar, jshell, jdb, jlink, jpackage, jstack, jcmd,
 * jconsole, jdeps, jfr, jimage, jinfo, jmap, jmod, jps, jrunscript, jstat and
 * the rest. They fail today at load, on the framework simply not being present,
 * not on anything they use.
 *
 * So this dylib is deliberately empty. dyld needs the file to exist with the
 * right install name; nothing needs it to contain anything.
 *
 * It does NOT re-export AppKit or CoreData, because OpenOSX does not have them
 * yet - an LC_REEXPORT_DYLIB naming a dylib that is not there fails at load,
 * which would be worse than the situation it replaces. When AppKit arrives, add
 * the re-export then. Foundation exists and is deliberately left out too, for
 * the same reason the rest of this project avoids convenient half-measures: a
 * binary that resolves Foundation symbols through Cocoa today and through
 * AppKit tomorrow is a binding that silently changes meaning.
 *
 * The one exported symbol exists only because a Mach-O dylib with a completely
 * empty export trie is an odd artifact to hand to a linker, and because it
 * gives `nm` something to show when confirming the framework was staged.
 */

/* Version of the umbrella itself, not of anything it wraps. */
const char OpenOSXCocoaVersionString[] = "OpenOSX Cocoa umbrella 0.1";
