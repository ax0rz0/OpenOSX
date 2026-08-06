{ stdenvNoCC }:

stdenvNoCC.mkDerivation {
  pname = "openosx-i3status-shim";
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
    printf ',[{"full_text":"OpenOSX"}]\n'
    sleep 5
done
EOF
    chmod 755 "$out/bin/i3status"
  '';
}
