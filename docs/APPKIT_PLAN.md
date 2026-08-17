# AppKit: what can be built now, and what cannot

AppKit is not all-or-nothing. Surveying it against the corpus of real macOS
binaries — and then adversarially refuting every optimistic claim — splits it
into three unequal parts:

| tier | count | meaning |
|---|---|---|
| plain C / re-export | 50 | no ObjC runtime needed, or a one-line forward to CoreGraphics |
| needs an ObjC class | 30 | needs real class metadata (a Cocotron fork, or shells) |
| needs a window server | 11 | cannot work at all without one |

## The single highest-leverage item is not AppKit at all

**`Cocoa.framework` is missing entirely**, and it is an umbrella — no code, just
`LC_REEXPORT_DYLIB` of AppKit, Foundation and CoreData.

Shipping an empty dylib with the right install name unblocks **java, javac,
keytool, jar, jshell, wish8.6 and 32 other binaries** that link Cocoa and then
never touch a single AppKit symbol. They fail today at load, on the framework's
absence, not on anything they use.

That is a few lines of nix for a third of the corpus.

## NSGeometry belongs in Foundation

`NSPoint`/`NSSize`/`NSRect` are **the same types** as `CGPoint`/`CGSize`/
`CGRect` — not similar, identical, with no conversion layer anywhere in AppKit
or Foundation. So the twelve measured geometry functions are each a one-line
forward:

```
NSEqualRects  -> CGRectEqualToRect      NSInsetRect   -> CGRectInset
NSPointInRect -> CGRectContainsPoint    NSZeroRect    -> a const global
```

`NSMouseInRect` is the only one with real logic — it has a flipped-coordinate
variant.

These live in **Foundation**, not AppKit. Task #21 as originally filed
("AppKit: geometry re-exports") is misfiled; nothing about them requires AppKit
to exist.

## Two claims the verifiers killed

Both are the same failure mode this project keeps meeting: a symbol that
*resolves* and then misbehaves, which is strictly worse than one that is
honestly absent.

**"The 81 non-class symbols are all constants, write them in a day."** Fourteen
of them are functions, not constants — `NSGetAlertPanel` returns an `NSPanel*`,
`NSRunInformationalAlertPanel` runs a modal event loop, `NSRectFill` and
`NSSetFocusRingStyle` draw into `[NSGraphicsContext currentContext]`, and
`NSDisableScreenUpdates` talks to a window server. Stubbing those to no-ops is
precisely the link-then-misbehave trap.

The remaining ~62 `NSString * const` globals are not free either, for two
reasons. They are not plain C — `NSString * const X = @"…"` emits a
`__CFConstantString` and needs the NSCFString bridge. And **their values are not
derivable from their names**:

```
NSFontAttributeName            == "NSFont"
NSForegroundColorAttributeName == "NSColor"
NSUnderlineStyleAttributeName  == "NSUnderline"
NSPasteboardTypeString         == "public.utf8-plain-text"
```

These are dictionary keys and pasteboard UTIs. Get one wrong and nothing
crashes and nothing works.

Genuinely plain-C right now: **three scalars.** `NSAppKitVersionNumber`,
`NSDarkGray`, `NSFontWeightRegular`.

**"`id NSApp = nil;` — one line, and it must ship first."** The premise is
right: `_NSApp` is a *data* symbol, so dyld binds it non-lazily at load with no
lazy-binding escape hatch, and it hard-fails. That is why it looks like a
trivial win.

But `NSApp` is a pointer to a live `NSApplication` that `sharedApplication`
sets. Defining it as nil satisfies dyld and then every `[NSApp …]` send hits a
nil receiver and returns 0 silently. Both corpus apps drive their whole
lifecycle through it — `-run`, `-sendEvent:`, `-terminate:`, delegate dispatch.
It converts a loud, diagnosable load failure into an app that launches and does
nothing, with no stack to read. `NSApp` is not independent of
`_OBJC_CLASS_$_NSApplication`; it ships with it or not at all.

## Order of work

1. **Cocoa umbrella.** Trivial, unblocks 32+ binaries, blocks on nothing.
2. **NSGeometry in Foundation.** Trivial, mechanical, verifiable against
   CoreGraphics which is already implemented and exported.
3. **Constants** — but only with values transcribed from the SDK headers and
   build-time asserted, the same discipline `Security.exports` and
   `SecureTransport.c` already use. Never from the symbol name.
4. **Class shells**, once there is a decision about forking Cocotron.
5. **Window-server surface** — not before the display stack is real.
