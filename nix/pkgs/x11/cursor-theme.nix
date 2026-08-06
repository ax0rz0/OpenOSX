{ lib
, runCommand
, vanilla-dmz
}:

runCommand "openosx-cursor-theme" { } ''
  mkdir -p "$out/usr/share/icons"
  cp -a ${vanilla-dmz}/share/icons/DMZ-White "$out/usr/share/icons/"
  chmod -R u+w "$out/usr/share/icons"

  mkdir -p "$out/usr/share/icons/default"
  cat > "$out/usr/share/icons/default/index.theme" <<'EOF'
[Icon Theme]
Name=Default
Comment=OpenOSX default cursor theme
Inherits=DMZ-White
EOF
''
