{ stdenvNoCC
, lib
, python3
, src            # src/Desktop/themes
, wmThemeScript  # tools/branding/make-wm-theme.py
}:

# OpenOSX Aqua: one theme directory carrying both halves of the desktop's look.
#
#   share/themes/OpenOSX-Aqua/xfwm4/    window decorations (generated)
#   share/themes/OpenOSX-Aqua/gtk-3.0/  widget styling (checked in)
#
# Sharing one name matters: xfwm4's `theme` key and xsettings' `Net/ThemeName`
# are separate settings, and giving them the same value is what keeps the
# titlebar and the window under it from looking like two different operating
# systems.
#
# The decorations are generated here rather than committed as PNGs, so the
# artwork stays reviewable as code and provably ours.
stdenvNoCC.mkDerivation {
  pname = "openosx-aqua-theme";
  version = "1";

  dontUnpack = true;
  nativeBuildInputs = [ python3 ];

  buildPhase = ''
    runHook preBuild
    python3 ${wmThemeScript} -o xfwm4
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    theme="$out/share/themes/OpenOSX-Aqua"
    mkdir -p "$theme"
    cp -r xfwm4 "$theme/xfwm4"
    cp -r ${src}/OpenOSX-Aqua/gtk-3.0 "$theme/gtk-3.0"

    # index.theme is what makes the theme show up in a chooser at all; without
    # it xfwm4 still finds the directory but the settings UI does not list it.
    cat > "$theme/index.theme" <<'EOF'
[Desktop Entry]
Type=X-GNOME-Metatheme
Name=OpenOSX Aqua
Comment=OpenOSX's desktop appearance
Encoding=UTF-8

[X-GNOME-Metatheme]
GtkTheme=OpenOSX-Aqua
MetacityTheme=OpenOSX-Aqua
IconTheme=Adwaita
CursorTheme=Adwaita
EOF

    runHook postInstall
  '';

  meta = with lib; {
    description = "OpenOSX Aqua desktop theme (xfwm4 decorations + GTK 3)";
    platforms = platforms.all;
  };
}
