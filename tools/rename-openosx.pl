#!/usr/bin/env perl
# OpenOSX deep-rename: PureDarwin -> OpenOSX, replayable after upstream syncs.
#
# Guards (lines NEVER touched):
#   - copyright/attribution lines
#   - live external references (github PureDarwin org, puredarwin.org, discord)
#   - the PUREDARWIN_LICENSE filename (word-boundary + lookahead below)
# Excluded paths entirely: licenses, vendored subtrees (tools/xnu-loader,
# tools/kc-tools), and this script itself.
use strict; use warnings;
my @files = split /\n/, `git ls-files`;
my $changed = 0;
for my $f (@files) {
    next if $f =~ m{^(PUREDARWIN_LICENSE\.txt|APPLE_LICENSE\.txt|APPLE_DRIVER_LICENSE\.txt)$};
    next if $f =~ m{^tools/(xnu-loader|kc-tools)/};
    next if $f =~ m{^docs/PUREDARWIN_ATTRIBUTION\.md$};
    next if $f =~ m{rename-openosx\.pl$};
    next unless -f $f;
    next if -B $f;  # skip binary files
    open my $in, "<", $f or next;
    my @lines = <$in>; close $in;
    my $dirty = 0;
    for (@lines) {
        next if /Copyright|\x{00A9}|APPLE_LICENSE_HEADER|Portions\s+of/i;
        next if m{github(\.com)?[:/]PureDarwin|puredarwin\.org|discord};
        my $before = $_;
        s/__PUREDARWIN__/__OPENOSX__/g;
        s/org\.puredarwin\./org.openosx./g;
        s/\bPUREDARWIN_(?!LICENSE)/OPENOSX_/g;
        s/\bPUREDARWIN\b/OPENOSX/g;
        s/puredarwin-/openosx-/g;
        s/\bpuredarwin\.img\b/openosx.img/g;
        s/\bPureDarwin\b/OpenOSX/g;
        s/\bpuredarwin\b/openosx/g;
        $dirty ||= ($before ne $_);
    }
    if ($dirty) {
        open my $out, ">", $f or die "write $f: $!";
        print $out @lines; close $out;
        $changed++;
    }
}
print "rewrote $changed files\n";
