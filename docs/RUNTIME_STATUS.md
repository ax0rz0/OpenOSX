# Running real macOS software: measured status

Everything here was produced by booting OpenOSX and executing unmodified
Homebrew binaries, not by measuring symbol tables. The distinction matters: a
binary can link perfectly and still die on first contact with a syscall or a
dyld fixup.

## What runs today

| runtime | result |
|---|---|
| **Python 3.11** | **PASS** — `ANSWER 42` |
| **Perl 5.36** | **PASS** — `ANSWER 42` |
| **Ruby 3.2** | **PASS** — `ANSWER 42` (after three RubyGems path warnings) |
| Java 21 | launcher runs; JVM fails to `dlopen` |
| Node 20 | needs openssl@3, libresolv |
| Lua 5.4 | needs `/usr/lib/libedit.3.dylib` |

Plus the Tier 0 CLI set already recorded in COMPAT_STATUS.md: `tree`, `lz4`,
`gzip`.

Reproduce with `tools/testing/run-openosx.sh`, or:

```bash
tools/compat/fetch-runtimes.sh
tools/compat/relocate-bottles.py /root/mac-runtimes --prefix /opt/compat-test
OPENOSX_COMPAT_CORPUS=/root/mac-runtimes nix build .#image-minimal --impure
```

The image smoke-tests the corpus at boot and prints PASS/FAIL per runtime.

## The single biggest finding: most "gaps" were not gaps

Perl, Ruby, Node and Lua all appeared broken with errors like:

    dyld: Library not loaded: @@HOMEBREW_CELLAR@@/ruby/3.2.2_1/lib/libruby.3.2.dylib

`@@HOMEBREW_CELLAR@@` and `@@HOMEBREW_PREFIX@@` are Homebrew's relocation
placeholders, rewritten by `brew` at install time. We pull bottles straight from
ghcr.io and never run brew, so dyld saw them verbatim. **That is an install step
nobody ran, not a compatibility gap.** `tools/compat/relocate-bottles.py`
rewrites them; perl and ruby started working immediately.

Similarly, java walked a chain of frameworks this project already builds and had
simply never staged into the minimal image: libz, then Cocoa, then Security,
then Foundation. Four boot cycles to discover what one transitive closure
traversal answers — see "measure the closure" below.

## Where java actually stands

`java` links six libraries and imports symbols from **two** of them:

```
  85 symbols  /usr/lib/libSystem.B.dylib
   9 symbols  @rpath/libjli.dylib          (ships in the JDK)
   0 symbols  libz / Cocoa / Security / ApplicationServices
```

Four are pure load-time existence checks. Cocoa and ApplicationServices are now
empty umbrellas in-tree for exactly this reason.

`libjli.dylib` then needs two Foundation classes and five selectors, all now
implemented: `NSAutoreleasePool` (`init`, `drain`), `NSBlockOperation`
(`blockOperationWithBlock:`, `start`), and
`performSelectorOnMainThread:withObject:waitUntilDone:`.

**The launcher now runs.** The failure moved from dyld ("image not found") to
java's own error handling:

    Error: dl failure on line 558 ... dlopen(libjvm.dylib): image not loaded

libjvm needs only libSystem (226 symbols) and libc++ (5), and **every one
resolves** — the only apparent miss, `dyld_stub_binder`, is re-exported from
libdyld and is a known false positive in `coverage.py`.

What distinguishes libjvm from everything that loads: it is the only one with
`HAS_TLV_DESCRIPTORS` and thread-local storage. `libjli` and Python have neither.
That makes dyld's thread-local variable setup for `dlopen`'d images the next
thing to investigate. `threadLocalVariables.c` and all three
`ImageLoaderMachO*.cpp` are compiled into our dyld, so this is a behaviour
question, not a missing-file one.

## Two lessons that cost build cycles

**Measure the closure, not the binary.** Scanning only the top-level executable
found java's blockers one boot at a time. `Foundation` was required by
`libjli.dylib`, not by `java`, so it never appeared. One transitive traversal
answered in seconds what four build-and-boot cycles had been answering one link
at a time.

**Staging a library is not providing a framework.** Foundation was built and
added to the image, and java still failed with "image not found", because it
existed only as `/usr/lib/libFoundation.dylib`. Binaries ask for
`/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation` — and
note the **C**: Foundation never moved off its historical version letter, where
almost every other framework is `Versions/A`, and a binary asking for C does not
fall back to A.
