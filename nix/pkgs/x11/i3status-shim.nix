{ stdenvNoCC }:

stdenvNoCC.mkDerivation {
  pname = "puredarwin-i3status-shim";
  version = "0.1";

  dontUnpack = true;
  dontFixup = true;

  installPhase = ''
    mkdir -p "$out/bin"
    cat > "$out/bin/i3status" <<'EOF'
#!/bin/sh

printf '{"version":1}\n'
printf '[\n'
printf '[]\n'

while :; do
    printf ',[{"full_text":"PureDarwin"}]\n'
    sleep 5
done
EOF
    chmod 755 "$out/bin/i3status"
  '';
}
