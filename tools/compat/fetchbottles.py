#!/usr/bin/env python3
"""
fetchbottles - pull real macOS x86_64 binaries to test OpenOSX against.

Homebrew publishes every formula as a "bottle": an ordinary tarball of Mach-O
binaries built for one specific macOS version, served from ghcr.io behind an
anonymous token. That makes it the cheapest supply of genuinely third-party
macOS executables - nothing here was built by our toolchain, so running one
proves something.

The macOS version a bottle targets is worth choosing deliberately, but not for
the reason you would expect. dyld's gate is `MachOFile::builtForPlatform`
(src/Libraries/dyld/upstream/dyld3/MachOFile.cpp:482), which receives minOS and
sdk and then compares only the platform ID - so a newer `minos` is NOT refused,
and a Monterey bottle loads exactly as far as a Big Sur one does. What actually
decides the outcome is whether libSystem exports every symbol the bottle
imports, and newer bottles simply tend to import newer symbols.

So target older releases to keep the import surface small, not because dyld
enforces a ceiling. Current bottles target Sonoma and later, so this walks a
formula's tags newest-first looking for the most recent build that still
targeted the requested macOS.

  fetchbottles.py lz4 tree xz --target big_sur -o corpus/
  fetchbottles.py --list-targets

No Mac, no Homebrew install and no `brew` client required.
"""
import argparse
import io
import json
import os
import sys
import tarfile
import urllib.error
import urllib.request

REGISTRY = 'https://ghcr.io'
REPO = 'homebrew/core'

# macOS release -> the minos its bottles carry. Every one of these passes the
# platform gate on Darwin 20.5; the number is a proxy for how large and how
# modern an import surface to expect, not a loadability verdict.
TARGETS = {
    'high_sierra': '10.13',
    'mojave': '10.14',
    'catalina': '10.15',
    'big_sur': '11.0',
    'monterey': '12.0',
    'ventura': '13.0',
    'sonoma': '14.0',
    'sequoia': '15.0',
}

INDEX_MEDIA = 'application/vnd.oci.image.index.v1+json'
MANIFEST_MEDIA = 'application/vnd.oci.image.manifest.v1+json'


def _get(url, token=None, accept=None, binary=False):
    req = urllib.request.Request(url)
    if token:
        req.add_header('Authorization', 'Bearer %s' % token)
    if accept:
        req.add_header('Accept', accept)
    with urllib.request.urlopen(req, timeout=90) as r:
        raw = r.read()
    return raw if binary else json.loads(raw)


def token_for(formula):
    url = ('%s/token?service=ghcr.io&scope=repository:%s/%s:pull'
           % (REGISTRY, REPO, formula))
    return _get(url)['token']


def tags_for(formula, token):
    url = '%s/v2/%s/%s/tags/list' % (REGISTRY, REPO, formula)
    return _get(url, token).get('tags', [])


def find_bottle(formula, token, tag, target):
    """Digest of the x86_64 bottle for `target` in this tag, or None.

    Homebrew names the arm64 variants `arm64_<target>`; the bare `<target>`
    suffix is Intel, which is what we want.
    """
    url = '%s/v2/%s/%s/manifests/%s' % (REGISTRY, REPO, formula, tag)
    try:
        index = _get(url, token, INDEX_MEDIA)
    except (urllib.error.HTTPError, urllib.error.URLError, ValueError):
        return None
    for man in index.get('manifests', []):
        ref = (man.get('annotations') or {}).get('org.opencontainers.image.ref.name', '')
        if ref.endswith('.' + target) and not ref.endswith('.arm64_' + target):
            return man['digest']
    return None


def fetch(formula, target, outdir):
    token = token_for(formula)
    tags = tags_for(formula, token)
    if not tags:
        return None, 'no tags'

    for tag in reversed(tags):                 # newest first
        digest = find_bottle(formula, token, tag, target)
        if not digest:
            continue
        url = '%s/v2/%s/%s/manifests/%s' % (REGISTRY, REPO, formula, digest)
        manifest = _get(url, token, MANIFEST_MEDIA)
        layers = manifest.get('layers') or []
        if not layers:
            continue
        blob = '%s/v2/%s/%s/blobs/%s' % (REGISTRY, REPO, formula, layers[0]['digest'])
        raw = _get(blob, token, binary=True)

        dest = outdir                          # the tarball already roots at <formula>/
        os.makedirs(dest, exist_ok=True)
        with tarfile.open(fileobj=io.BytesIO(raw)) as tf:
            # Bottles are Homebrew's own artefacts, but extract defensively.
            members = [m for m in tf.getmembers()
                       if not m.name.startswith('/') and '..' not in m.name.split('/')]
            try:
                tf.extractall(dest, members=members, filter='data')
            except TypeError:                  # filter= is Python 3.12+
                tf.extractall(dest, members=members)
        return {'formula': formula, 'tag': tag, 'target': target,
                'bytes': len(raw), 'path': dest}, None
    return None, 'no %s bottle in any of %d tags' % (target, len(tags))


def executables(root):
    """Every Mach-O file under root, by magic rather than by permission bits."""
    magics = (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xce\xfa\xed\xfe')
    found = []
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            p = os.path.join(dirpath, f)
            if os.path.islink(p):
                continue
            try:
                with open(p, 'rb') as fh:
                    if fh.read(4) in magics:
                        found.append(p)
            except OSError:
                continue
    return sorted(found)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('formulae', nargs='*')
    ap.add_argument('--target', default='big_sur',
                    help='macOS release the bottle was built for (default big_sur)')
    ap.add_argument('-o', '--out', default='corpus', help='output directory')
    ap.add_argument('--list-targets', action='store_true')
    args = ap.parse_args()

    if args.list_targets:
        print('%-14s %s' % ('target', 'minos its bottles carry'))
        for name, minos in TARGETS.items():
            print('%-14s %s' % (name, minos))
        print('\nAll of these pass the platform gate; older targets simply import less.')
        return
    if not args.formulae:
        raise SystemExit('give at least one formula, or --list-targets')

    os.makedirs(args.out, exist_ok=True)
    got = []
    for f in args.formulae:
        try:
            info, err = fetch(f, args.target, args.out)
        except (urllib.error.HTTPError, urllib.error.URLError) as e:
            info, err = None, str(e)
        if info:
            n = len(executables(info['path']))
            print('  %-12s %-22s %6.1f KiB  %d Mach-O' %
                  (f, info['tag'], info['bytes'] / 1024.0, n))
            got.append(info)
        else:
            print('  %-12s %s' % (f, err))

    if got:
        man = os.path.join(args.out, 'bottles.json')
        json.dump(got, open(man, 'w'), indent=1)
        print('\n%d bottles in %s (manifest: %s)' % (len(got), args.out, man))


if __name__ == '__main__':
    main()
