# XFCE desktop: the session manager, window manager, panel, settings daemon
# and the applications that ship with them.
#
# Most XFCE "hard" dependencies are optional at configure time
# (XDT_CHECK_OPTIONAL_PACKAGE), so components missing from this tree are
# disabled rather than fatal.
{ atspi2CoreBuild
, cairoBuild
, cairoGobjectBuild
, darwinCrossToolchain
, dbusBuild
, exoSrc
, expatBuild
, fontconfigBuild
, freetype2Build
, fribidiBuild
, garconSrc
, gdkPixbufBuild
, glibBuild
, gtk3Build
, harfbuzzBuild
, isDarwin
, lib
, libSystemBuild
, libdisplayInfoBuild
, libepoxyBuild
, libffiBuild
, libiconvBuild
, libpngBuild
, libwnckBuild
, libxfce4uiSrc
, nativeLd
, nativeMesonToolsDir
, pangoBuild
, pcre2Build
, pkgs
, startupNotificationBuild
, vteBuild
, xcbBuild
, xfce4AppfinderSrc
, xfce4PanelSrc
, xfce4SessionSrc
, xfce4SettingsSrc
, xfce4TerminalSrc
, xfconfSrc
, xfdesktopSrc
, xfwm4Src
, xlibBuild
, xvfbLibICEBuild
, xvfbLibSMBuild
, xvfbLibXauBuild
, xvfbLibXcompositeBuild
, xvfbLibXcursorBuild
, xvfbLibXdamageBuild
, xvfbLibXdmcpBuild
, xvfbLibXextBuild
, xvfbLibXfixesBuild
, xvfbLibXiBuild
, xvfbLibXineramaBuild
, xvfbLibXpresentBuild
, xvfbLibXrandrBuild
, xvfbLibXrenderBuild
, xvfbLibXresBuild
, xvfbPixmanBuild
, xvfbZlibBuild
}:

let
  xfconfBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      shared = true;
      nativeMesonTools = nativeMesonToolsDir;
      pname = "puredarwin-xfconf";
      version = "4.20.0";
      src = xfconfSrc;
      deps = [
        glibBuild
        pcre2Build
        libffiBuild
        xvfbZlibBuild
        libiconvBuild
        libxfce4utilBuild
      ];
      postInstallExtra = ''
        substituteInPlace "$out/share/dbus-1/services/org.xfce.Xfconf.service" \
          --replace "$out/lib/xfce4/xfconf/xfconfd" "/lib/xfce4/xfconf/xfconfd"
      '';
      configureFlags = [
        "--disable-visibility"
        "--disable-gtk-doc"
        "--disable-vala"
        "--without-html-dir"
        "--disable-linker-opts"
      ];
    };
  libxfce4utilBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/xfce/libxfce4util.nix {
      nativeMesonTools = nativeMesonToolsDir;
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      inherit (pkgs.xfce) libxfce4util;
      glib = glibBuild;
      pcre2 = pcre2Build;
      libffi = libffiBuild;
      zlib = xvfbZlibBuild;
      libiconv = libiconvBuild;
    };
  libxfce4uiBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      shared = true;
      nativeMesonTools = nativeMesonToolsDir;
      pname = "puredarwin-libxfce4ui";
      version = "4.20.2";
      src = libxfce4uiSrc;
      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext ];
      configureFlags = [
        "--disable-visibility"
        # see xfconfBuild: the XDT probe tests the build linker, not ld64
        "--disable-linker-opts"
        "--disable-gtk-doc"
        "--without-html-dir"
        # libgtop is only the About dialog's System Info widget, and
        # skipping it also disables gudev - GObject bindings for udev,
        # which cannot exist on Darwin. configure.ac nests the GUDEV and
        # EPOXY checks inside "if GLIBTOP_FOUND", so this is the switch
        # that keeps Linux device enumeration out of the build entirely.
        "--disable-glibtop"
        "--disable-gladeui2"
        "--disable-wayland"
      ];
    };
  xfwm4Build =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      guestPrefix = true;
      pname = "puredarwin-xfwm4";
      version = "4.20.0";
      src = xfwm4Src;
      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild libxfce4uiBuild libwnckBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibXresBuild xvfbLibXineramaBuild
        xvfbLibXcompositeBuild xvfbLibXdamageBuild xvfbLibXpresentBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext ];
      configureFlags = [
        "--x-includes=${lib.getDev xlibBuild}/include"
        "--x-libraries=${xlibBuild}/lib"
        "--disable-gtk-doc"
        "--without-html-dir"
        "--enable-compositor"
        "--enable-xpresent"
        "--enable-epoxy"
        "--enable-render"
        "--enable-randr"
        "--enable-xsync"
        # xi2 stays off, matching upstream's default: its AC_CHECK_LIB
        # link probe does not survive PureDarwin's static libXi, and input
        # arrives through the puredarwininput driver regardless.
        "--enable-startup-notification"
      ];
    };
  libxfce4windowingBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/xfce/libxfce4windowing.nix {
      nativeMesonTools = nativeMesonToolsDir;
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      # the 4.20.6 release tarball, not nixpkgs' src: that checkout is
      # 4.20.5 (its version attr disagrees) and xfce4-panel 4.20.8 needs
      # libxfce4windowing-0 >= 4.20.6.
      version = "4.20.6";
      src = pkgs.fetchurl {
        url = "https://archive.xfce.org/src/xfce/libxfce4windowing/4.20/libxfce4windowing-4.20.6.tar.bz2";
        sha256 = "sha256-LQa2pWfJZa+8oaUUGfxyj9g70EYOMKtiw0Vk1eCqyeM=";
      };
      glib = glibBuild;
      pcre2 = pcre2Build;
      libffi = libffiBuild;
      zlib = xvfbZlibBuild;
      libiconv = libiconvBuild;
      cairo = cairoBuild;
      cairoGobject = cairoGobjectBuild;
      pixman = xvfbPixmanBuild;
      pango = pangoBuild;
      fribidi = fribidiBuild;
      harfbuzz = harfbuzzBuild;
      freetype2 = freetype2Build;
      fontconfig = fontconfigBuild;
      expat = expatBuild;
      gdkPixbuf = gdkPixbufBuild;
      libepoxy = libepoxyBuild;
      atspi2Core = atspi2CoreBuild;
      dbus = dbusBuild;
      libpng = libpngBuild;
      gtk3 = gtk3Build;
      libwnck = libwnckBuild;
      libdisplayInfo = libdisplayInfoBuild;
      libX11 = xlibBuild;
      libxcb = xcbBuild;
      libXau = xvfbLibXauBuild;
      libXdmcp = xvfbLibXdmcpBuild;
      libXext = xvfbLibXextBuild;
      libXi = xvfbLibXiBuild;
      libXrender = xvfbLibXrenderBuild;
      libXrandr = xvfbLibXrandrBuild;
      libXfixes = xvfbLibXfixesBuild;
      libXcursor = xvfbLibXcursorBuild;
      libXres = xvfbLibXresBuild;
      startupNotification = startupNotificationBuild;
      inherit (pkgs) xorgproto;
    };
  garconBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      # shared for the same reason as xfconf: xfce4-panel dlopens plugin
      # modules that link this too, so a static copy would be registered
      # once in the panel binary and again in each plugin.
      shared = true;
      nativeMesonTools = nativeMesonToolsDir;
      pname = "puredarwin-garcon";
      version = "4.20.0";
      src = garconSrc;
      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild libxfce4uiBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext ];
      configureFlags = [
        "--disable-linker-opts"
        "--disable-gtk-doc"
        "--without-html-dir"
      ];
    };
  exoBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      shared = true;
      nativeMesonTools = nativeMesonToolsDir;
      pname = "puredarwin-exo";
      version = "4.20.0";
      src = exoSrc;
      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild libxfce4uiBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext ];
      configureFlags = [
        "--disable-visibility"
        "--disable-linker-opts"
        "--disable-gtk-doc"
        "--without-html-dir"
      ];
    };
  xfce4SessionBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      guestPrefix = true;
      pname = "puredarwin-xfce4-session";
      version = "4.20.4";
      src = xfce4SessionSrc;
      preConfigureExtra = ''
        export ICEAUTH=/bin/iceauth
        export GLIB_COMPILE_RESOURCES=${pkgs.glib.dev}/bin/glib-compile-resources
      '';

      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild libxfce4uiBuild
        libwnckBuild libxfce4windowingBuild libdisplayInfoBuild
        garconBuild exoBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibXresBuild xvfbLibXineramaBuild
        xvfbLibXcompositeBuild xvfbLibXdamageBuild xvfbLibXpresentBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext ];
      postInstallExtra = ''
        substituteInPlace "$out/etc/xdg/xfce4/xinitrc" \
          --replace "$out/etc/xdg/xfce4" "/etc/xdg/xfce4"
      '';
      configureFlags = [
        "--x-includes=${lib.getDev xlibBuild}/include"
        "--x-libraries=${xlibBuild}/lib"
        "--disable-gtk-doc"
        "--without-html-dir"
        "--disable-linker-opts"
        "--with-xsession-prefix=${placeholder "out"}"
      ];
    };
  xfce4PanelBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      guestPrefix = true;
      # shared, unlike the other leaf executables: the panel's plugins are
      # libtool -module targets that it dlopens at runtime, and with
      # --disable-shared they come out as libactions.a and friends, which
      # cannot be loaded - the panel would start as an empty bar.
      shared = true;
      nativeMesonTools = nativeMesonToolsDir;
      pname = "puredarwin-xfce4-panel";
      version = "4.20.8";
      src = xfce4PanelSrc;
      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild libxfce4uiBuild
        libwnckBuild libxfce4windowingBuild libdisplayInfoBuild
        garconBuild exoBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibXresBuild xvfbLibXineramaBuild
        xvfbLibXcompositeBuild xvfbLibXdamageBuild xvfbLibXpresentBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext pkgs.python3 ];
      postPatchExtra = ''
        patchShebangs .
        source ${./pkgs/xfce/no-symbol-aliases.sh}
      '';
      preConfigureExtra = ''
        export GLIB_COMPILE_RESOURCES=${pkgs.glib.dev}/bin/glib-compile-resources
      '';
      configureFlags = [
        "--x-includes=${lib.getDev xlibBuild}/include"
        "--x-libraries=${xlibBuild}/lib"
        "--disable-gtk-doc"
        "--without-html-dir"
        "--disable-visibility"
        "--disable-linker-opts"
      ];
    };
  xfdesktopBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      guestPrefix = true;
      pname = "puredarwin-xfdesktop";
      version = "4.20.2";
      src = xfdesktopSrc;
      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild libxfce4uiBuild
        libwnckBuild libxfce4windowingBuild libdisplayInfoBuild
        garconBuild exoBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibXresBuild xvfbLibXineramaBuild
        xvfbLibXcompositeBuild xvfbLibXdamageBuild xvfbLibXpresentBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext ];
      postPatchExtra = ''
        substituteInPlace src/Makefile.in \
          --replace "-export-dynamic" "-Wl,-export_dynamic"
      '';
      preConfigureExtra = ''
        export GLIB_COMPILE_RESOURCES=${pkgs.glib.dev}/bin/glib-compile-resources
      '';
      configureFlags = [
        "--x-includes=${lib.getDev xlibBuild}/include"
        "--x-libraries=${xlibBuild}/lib"
        "--disable-gtk-doc"
        "--without-html-dir"
      ];
    };
  xfce4TerminalBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/xfce/xfce4-terminal.nix {
      nativeMesonTools = nativeMesonToolsDir;
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      version = "1.2.0";
      src = xfce4TerminalSrc;
      glib = glibBuild;
      pcre2 = pcre2Build;
      libffi = libffiBuild;
      zlib = xvfbZlibBuild;
      libiconv = libiconvBuild;
      cairo = cairoBuild;
      cairoGobject = cairoGobjectBuild;
      pixman = xvfbPixmanBuild;
      pango = pangoBuild;
      fribidi = fribidiBuild;
      harfbuzz = harfbuzzBuild;
      freetype2 = freetype2Build;
      fontconfig = fontconfigBuild;
      expat = expatBuild;
      gdkPixbuf = gdkPixbufBuild;
      libepoxy = libepoxyBuild;
      atspi2Core = atspi2CoreBuild;
      dbus = dbusBuild;
      libX11 = xlibBuild;
      libxcb = xcbBuild;
      libXau = xvfbLibXauBuild;
      libXdmcp = xvfbLibXdmcpBuild;
      libXext = xvfbLibXextBuild;
      libXi = xvfbLibXiBuild;
      libXrender = xvfbLibXrenderBuild;
      libXrandr = xvfbLibXrandrBuild;
      libXfixes = xvfbLibXfixesBuild;
      libXcursor = xvfbLibXcursorBuild;
      libpng = libpngBuild;
      glibNative = pkgs.glib.dev;
      inherit (pkgs) libxslt libxml2 docbook-xsl-nons;
      gtk3 = gtk3Build;
      vte = vteBuild;
      libxfce4ui = libxfce4uiBuild;
      libxfce4util = libxfce4utilBuild;
      xfconf = xfconfBuild;
      inherit (pkgs) xorgproto;
    };
  xfce4SettingsBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      guestPrefix = true;
      pname = "puredarwin-xfce4-settings";
      version = "4.20.5";
      src = xfce4SettingsSrc;
      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild libxfce4uiBuild
        garconBuild exoBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext pkgs.python3 pkgs.libxml2 ];
      preConfigureExtra = ''
        export GLIB_COMPILE_RESOURCES=${pkgs.glib.dev}/bin/glib-compile-resources
        export GDBUS_CODEGEN=${pkgs.glib.dev}/bin/gdbus-codegen
      '';
      configureFlags = [
        "--x-includes=${lib.getDev xlibBuild}/include"
        "--x-libraries=${xlibBuild}/lib"
        "--disable-gtk-doc"
        "--without-html-dir"
        "--disable-linker-opts"
        "--disable-libxklavier"
        "--disable-xorg-libinput"
        "--disable-libnotify"
        "--disable-wayland"
        "--disable-gtk-layer-shell"
      ];
    };
  xfce4AppfinderBuild =
    if isDarwin then null else pkgs.callPackage ./pkgs/x11/xorg-cross-lib.nix {
      inherit darwinCrossToolchain nativeLd;
      libSystem = libSystemBuild;
      guestPrefix = true;
      pname = "puredarwin-xfce4-appfinder";
      version = "4.20.0";
      src = xfce4AppfinderSrc;
      deps = [
        glibBuild pcre2Build libffiBuild xvfbZlibBuild libiconvBuild
        cairoBuild cairoGobjectBuild xvfbPixmanBuild
        pangoBuild fribidiBuild harfbuzzBuild freetype2Build
        fontconfigBuild expatBuild
        gdkPixbufBuild libepoxyBuild atspi2CoreBuild dbusBuild libpngBuild
        gtk3Build
        libxfce4utilBuild xfconfBuild libxfce4uiBuild
        libwnckBuild libxfce4windowingBuild libdisplayInfoBuild
        garconBuild exoBuild
        xlibBuild xcbBuild xvfbLibXauBuild xvfbLibXdmcpBuild
        xvfbLibXextBuild xvfbLibXiBuild xvfbLibXrenderBuild
        xvfbLibXrandrBuild xvfbLibXfixesBuild xvfbLibXcursorBuild
        xvfbLibXresBuild xvfbLibXineramaBuild
        xvfbLibXcompositeBuild xvfbLibXdamageBuild xvfbLibXpresentBuild
        xvfbLibICEBuild xvfbLibSMBuild
        startupNotificationBuild
        pkgs.xorgproto
      ];
      nativeDeps = [ pkgs.gettext ];
      configureFlags = [
        "--x-includes=${lib.getDev xlibBuild}/include"
        "--x-libraries=${xlibBuild}/lib"
        "--disable-gtk-doc"
        "--without-html-dir"
        "--disable-linker-opts"
      ];
    };
in
{
  inherit
    xfconfBuild
    libxfce4utilBuild
    libxfce4uiBuild
    xfwm4Build
    libxfce4windowingBuild
    garconBuild
    exoBuild
    xfce4SessionBuild
    xfce4PanelBuild
    xfdesktopBuild
    xfce4TerminalBuild
    xfce4SettingsBuild
    xfce4AppfinderBuild
    ;
}
