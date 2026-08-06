#!/usr/bin/env perl
# OpenOSX deep-rename: PureDarwin -> OpenOSX, replayable after upstream syncs.
#
# Pass 1 rewrites file CONTENTS; pass 2 renames FILES/DIRS by applying the
# exact same substitution rules to each tracked path — so on-disk names always
# match what pass 1 turned the references into.
#
# Guards (lines NEVER touched):
#   - copyright/attribution lines
#   - live external references (github PureDarwin org, puredarwin.org, discord)
#   - the PUREDARWIN_LICENSE filename (lookahead below)
# Excluded paths entirely: license files (incl. PUREDARWIN-LICENSES.md),
# vendored subtrees (tools/xnu-loader, tools/kc-tools), this script.
use strict; use warnings;
use File::Path qw(make_path);
use File::Basename qw(dirname);

sub excluded {
    my ($f) = @_;
    return 1 if $f =~ m{^(PUREDARWIN_LICENSE\.txt|APPLE_LICENSE\.txt|APPLE_DRIVER_LICENSE\.txt)$};
    return 1 if $f =~ m{PUREDARWIN-LICENSES\.md$};
    return 1 if $f =~ m{^tools/(xnu-loader|kc-tools)/};
    return 1 if $f =~ m{^docs/PUREDARWIN_ATTRIBUTION\.md$};
    return 1 if $f =~ m{rename-openosx.*\.pl$};
    return 0;
}

sub apply_rules {
    my ($s) = @_;
    $s =~ s/__PUREDARWIN__/__OPENOSX__/g;
    $s =~ s/org\.puredarwin\./org.openosx./g;
    $s =~ s/\bPUREDARWIN_(?!LICENSE)/OPENOSX_/g;
    $s =~ s/\bPUREDARWIN\b/OPENOSX/g;
    $s =~ s/puredarwin-/openosx-/g;
    $s =~ s/\bpuredarwin\.img\b/openosx.img/g;
    $s =~ s/\bPureDarwin\b/OpenOSX/g;
    $s =~ s/\bpuredarwin\b/openosx/g;
    return $s;
}

# ---- Pass 1: contents ----
my @files = split /\n/, `git ls-files`;
my $changed = 0;
for my $f (@files) {
    next if excluded($f);
    next unless -f $f;
    next if -B $f;  # skip binary files
    open my $in, "<", $f or next;
    my @lines = <$in>; close $in;
    my $dirty = 0;
    for (@lines) {
        next if /Copyright|\x{00A9}|APPLE_LICENSE_HEADER|Portions\s+of/i;
        next if m{github(\.com)?[:/]PureDarwin|puredarwin\.org|discord};
        my $before = $_;
        $_ = apply_rules($_);
        $dirty ||= ($before ne $_);
    }
    if ($dirty) {
        open my $out, ">", $f or die "write $f: $!";
        print $out @lines; close $out;
        $changed++;
    }
}
print "contents: rewrote $changed files\n";

# ---- Pass 2: paths (same rules => consistent with rewritten references) ----
my $moved = 0;
for my $f (split /\n/, `git ls-files`) {
    next if excluded($f);
    my $new = apply_rules($f);
    next if $new eq $f;
    make_path(dirname($new));
    system("git", "mv", $f, $new) == 0 or die "git mv $f -> $new failed";
    $moved++;
    print "moved: $f -> $new\n";
}
print "paths: moved $moved files\n";
