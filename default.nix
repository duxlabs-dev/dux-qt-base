{
  lib,
  nix-gitignore,
  stdenv,

  pkg-config,
  cmake,
  ninja,
  spirv-tools,
  qt6,
  cli11,
  wayland,
  wayland-protocols,
  wayland-scanner,
  xorg,
  libxcb ? xorg.libxcb,
  libdrm,
  libgbm ? null,
  vulkan-headers,
  pipewire,
  pam,
  polkit,
  glib,

  gitRev ? (let
    headExists = builtins.pathExists ./.git/HEAD;
    headContent = builtins.readFile ./.git/HEAD;
  in if headExists
     then (let
       matches = builtins.match "ref: refs/heads/(.*)\n" headContent;
     in if matches != null
        then builtins.readFile ./.git/refs/heads/${builtins.elemAt matches 0}
        else headContent)
     else "unknown"),

  debug ? false,
  withQtSvg ? true,
  withWayland ? true,
  withX11 ? true,
  withPipewire ? true,
  withPam ? true,
  withHyprland ? true,
  withI3 ? true,
  withPolkit ? true,
  withNetworkManager ? true,
}: stdenv.mkDerivation {
  pname = "dux-qt-base${lib.optionalString debug "-debug"}";
  version = "0.3.2";
  src = nix-gitignore.gitignoreSource "/default.nix\n" ./.;

  outputs = [ "out" "dev" ];

  # qtbase's setup hook aborts without this when no wrapQtAppsHook is present
  dontWrapQtApps = true;

  nativeBuildInputs = [
    cmake
    ninja
    spirv-tools
    pkg-config
  ]
  ++ lib.optionals withWayland [
    qt6.qtwayland # qtwaylandscanner required at build time
    wayland-scanner
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtdeclarative
    libdrm
    cli11
  ]
  ++ lib.optional withQtSvg qt6.qtsvg
  ++ lib.optionals withWayland [ qt6.qtwayland wayland wayland-protocols ]
  ++ lib.optionals (withWayland && libgbm != null) [ libgbm vulkan-headers ]
  ++ lib.optional withX11 libxcb
  ++ lib.optional withPam pam
  ++ lib.optional withPipewire pipewire
  ++ lib.optionals withPolkit [ polkit glib ];

  cmakeBuildType = if debug then "Debug" else "RelWithDebInfo";

  cmakeFlags = [
    (lib.cmakeFeature "DISTRIBUTOR" "Nix Flake (dux-qt-base)")
    (lib.cmakeBool "DISTRIBUTOR_DEBUGINFO_AVAILABLE" true)
    (lib.cmakeFeature "GIT_REVISION" gitRev)
    (lib.cmakeBool "WAYLAND" withWayland)
    (lib.cmakeBool "SCREENCOPY" (libgbm != null))
    (lib.cmakeBool "WAYLAND_RENDER_CONTROL" (libgbm != null))
    (lib.cmakeBool "X11" withX11)
    (lib.cmakeBool "SERVICE_PIPEWIRE" withPipewire)
    (lib.cmakeBool "SERVICE_PAM" withPam)
    (lib.cmakeBool "SERVICE_NETWORKMANAGER" withNetworkManager)
    (lib.cmakeBool "SERVICE_POLKIT" withPolkit)
    (lib.cmakeBool "HYPRLAND" withHyprland)
    (lib.cmakeBool "I3" withI3)
  ];

  # How to get debuginfo in gdb from a release build:
  # 1. build `dux-qt-base.debug`
  # 2. set NIX_DEBUG_INFO_DIRS="<dux-qt-base.debug store path>/lib/debug"
  # 3. launch gdb / coredumpctl and debuginfo will work
  separateDebugInfo = !debug;
  dontStrip = debug;

  meta = with lib; {
    homepage = "https://github.com/duxlabs-dev/dux-qt-base";
    description = "Quickshell QML runtime as a shared library with a C entry point";
    license = licenses.lgpl3Only;
    platforms = platforms.linux;
  };
}
