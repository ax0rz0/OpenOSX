#!/usr/bin/env bash
#
# Mint the TLS test fixtures. Deterministic and entirely offline, so the suite
# runs inside a Nix sandbox with no network.
#
# The negative fixtures are the point of this file. A TLS layer that accepts an
# expired certificate, an unknown issuer, or a mismatched hostname is worse than
# no TLS at all, because callers believe they are protected. Each of those cases
# gets a certificate here so the test suite can prove we reject it.
#
#   ca.pem / ca.key            test CA
#   good.pem / good.key        valid leaf for "localhost", signed by the CA
#   expired.pem / expired.key  leaf signed by the CA, validity window in 2001
#   selfsigned.pem/.key        untrusted leaf, not signed by the CA at all
#   wronghost.pem/.key         valid chain, but SAN is wrong.example
set -euo pipefail

OUT="${1:-$(dirname "$0")/fixtures}"
mkdir -p "$OUT"
cd "$OUT"

# A fixed serial and no random serial generation keeps output reproducible-ish;
# key material is still random, which is fine - nothing pins these hashes.
say() { printf '  %s\n' "$*"; }

say "test CA"
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem \
    -days 3650 -subj "/CN=OpenOSX Test CA" 2>/dev/null

gen_leaf() {
    local name="$1" cn="$2" san="$3"
    openssl req -newkey rsa:2048 -nodes -keyout "$name.key" -out "$name.csr" \
        -subj "/CN=$cn" 2>/dev/null
    cat > "$name.ext" <<EXT
subjectAltName = $san
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
EXT
    shift 3
    openssl x509 -req -in "$name.csr" -CA ca.pem -CAkey ca.key -CAcreateserial \
        -out "$name.pem" -extfile "$name.ext" "$@" 2>/dev/null
    rm -f "$name.csr" "$name.ext"
}

say "valid leaf (localhost)"
gen_leaf good localhost "DNS:localhost,IP:127.0.0.1" -days 3650

# A negative -days does not work ("end date before start date"), so pin an
# explicit validity window in the past. -not_before/-not_after are supported by
# the openssl this builds against; if a much older openssl ever has to be
# supported, the fallback is `openssl ca` with -startdate/-enddate.
say "expired leaf (valid 2001-01-01 .. 2001-01-02)"
gen_leaf expired localhost "DNS:localhost,IP:127.0.0.1" \
    -not_before 20010101000000Z -not_after 20010102000000Z

say "wrong-hostname leaf (SAN=wrong.example)"
gen_leaf wronghost wrong.example "DNS:wrong.example" -days 3650

say "untrusted self-signed leaf"
openssl req -x509 -newkey rsa:2048 -nodes -keyout selfsigned.key \
    -out selfsigned.pem -days 3650 -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" 2>/dev/null

say "verifying the fixtures are what we claim"
# good must verify against the CA; expired and selfsigned must NOT.
openssl verify -CAfile ca.pem good.pem >/dev/null 2>&1 \
    || { echo "FIXTURE BUG: good.pem does not verify against the CA"; exit 1; }
if openssl verify -CAfile ca.pem expired.pem >/dev/null 2>&1; then
    echo "FIXTURE BUG: expired.pem verified - it should be expired"; exit 1
fi
if openssl verify -CAfile ca.pem selfsigned.pem >/dev/null 2>&1; then
    echo "FIXTURE BUG: selfsigned.pem verified against our CA"; exit 1
fi
say "fixtures OK in $OUT"
