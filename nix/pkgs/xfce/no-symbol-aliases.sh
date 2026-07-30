# Neuter XFCE's xdt-gen-visibility. Every XFCE component vendors a copy and
# generates a <lib>-visibility.{c,h} pair from its .symbols file, emitting glib's
# internal-alias pattern:
#
#   header:  extern __typeof(foo) IA__foo __attribute__((visibility("hidden")));
#            #define foo IA__foo
#   source:  extern __typeof(foo) foo __attribute__((alias("IA__foo"),
#                                                    visibility("default")));
#
# This MUST be paired with -Dvisibility=false (meson) or --disable-visibility
# (autotools), which keeps gnu_symbol_visibility at 'default'. With the generator
# silenced but the default still 'hidden', every public symbol would be hidden
# and the library would export nothing.
sed -i \
  -e "s/^def header_decls_for_symbol(.*) -> str:\$/&\n    return ''/" \
  -e "s/^def source_decls_for_symbol(.*) -> str:\$/&\n    return ''/" \
  xdt-gen-visibility
