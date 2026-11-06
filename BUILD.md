# Build instructions
Instructions for building from source and for distro packagers.

dux-qt-base is the quickshell runtime built as a shared library. There is no
executable: consumers link `libdux-qt-base.so` and call `dqb_main()` from
`dux_qt_base.h`. Go consumers use `#cgo pkg-config: dux-qt-base`.

## What gets installed
`cmake --install` writes exactly these files, nothing else:

- `<libdir>/libdux-qt-base.so.0.3.2`, plus the `.so.0` and `.so` symlinks
- `<libdir>/pkgconfig/dux-qt-base.pc`
- `<prefix>/include/dux-qt-base/dux_qt_base.h`

QML modules are compiled into the library. No QML, desktop, or icon files land
on disk. The configure-time warning that QML modules will not be installed is
expected; leave `INSTALL_QML_PREFIX` and `INSTALL_QMLDIR` unset.

Split packages as runtime (`.so.0`, `.so.0.3.2`) and dev (`.so` symlink, `.pc`,
header). Ready-made recipes live in `distro/` (arch, fedora, opensuse, debian,
void) and `default.nix`.

## Packaging
Set `-DDISTRIBUTOR="your distribution platform"` so bug reports identify the
package, for example `AUR (dux-qt-base-git)` or `Fedora COPR (dux-qt-base)`.
Pass `-DGIT_REVISION=<commit or tag>` when building from a tarball, otherwise
cmake asks `git rev-parse HEAD`.

Set `-DCMAKE_INSTALL_LIBDIR` explicitly (`lib`, `lib64`, or the multiarch
triplet dir); the `.pc` file is generated from it.

The upstream `USE_JEMALLOC` and `CRASH_HANDLER` options default to `OFF` and no
packaging enables them. `BUILD_EXECUTABLE` is a dev-only option, keep it off.

## Dependencies
Base dependencies, names vary by distro:

- `cmake`, `ninja`, `pkg-config` (build-time)
- `qt6base`, `qt6declarative`, `qt6wayland`
- `qtshadertools`, `spirv-tools` (build-time)
- `cli11` (header-only, build-time)
- `libdrm`, `libxkbcommon`

Quickshell relies on private Qt APIs and **MUST** be rebuilt against each Qt
release or crashes via ABI mismatches will occur. Private headers are needed for
`qt6base`, `qt6declarative`, and `qt6wayland`; on Qt >= 6.9 cmake looks them up
as the `QuickPrivate` and `WaylandClientPrivate` components, so install the
distro's private-devel packages where they are split out.

At least Qt 6.8 is required. `qt6svg` is recommended at runtime, otherwise svg
images and icons will not load.

All features are enabled by default and some have their own dependencies.

### Unix Sockets
Unix socket clients and servers for IPC. No additional dependencies.

To disable: `-DSOCKETS=OFF`

### Wayland
Wayland support. Subfeatures exist for each integration.

To disable: `-DWAYLAND=OFF`

Dependencies: `wayland` (libwayland-client), `wayland-scanner` (build-time),
`wayland-protocols >= 1.41`.

Subfeatures with no extra dependencies:
- `-DWAYLAND_WLR_LAYERSHELL=OFF` ([zwlr-layer-shell-v1])
- `-DWAYLAND_SESSION_LOCK=OFF` ([ext-session-lock-v1])
- `-DWAYLAND_TOPLEVEL_MANAGEMENT=OFF` ([zwlr-foreign-toplevel-management-v1])

[zwlr-layer-shell-v1]: https://wayland.app/protocols/wlr-layer-shell-unstable-v1
[ext-session-lock-v1]: https://wayland.app/protocols/ext-session-lock-v1
[zwlr-foreign-toplevel-management-v1]: https://wayland.app/protocols/wlr-foreign-toplevel-management-unstable-v1

#### Screencopy
Streams video from monitors and toplevels.

To disable: `-DSCREENCOPY=OFF`

Dependencies: `libgbm`, `egl`, `vulkan-headers` (build-time).

Per-protocol switches: `-DSCREENCOPY_ICC=OFF` ([ext-image-copy-capture-v1]),
`-DSCREENCOPY_WLR=OFF` ([zwlr-screencopy-v1]),
`-DSCREENCOPY_HYPRLAND_TOPLEVEL=OFF` ([hyprland-toplevel-export-v1]).

[ext-image-copy-capture-v1]: https://wayland.app/protocols/ext-image-copy-capture-v1
[zwlr-screencopy-v1]: https://wayland.app/protocols/wlr-screencopy-unstable-v1
[hyprland-toplevel-export-v1]: https://wayland.app/protocols/hyprland-toplevel-export-v1


#### Render Control
Use quickshell's render control for Wayland windows and report changed frame regions
to the compositor. Enable it in a shell with `//@ pragma RenderControl`.

To disable: `-DWAYLAND_RENDER_CONTROL=OFF`

Dependencies:
- `libgbm`
- `vulkan-headers` (build-time)

### X11
Panel windows for X11.

To disable: `-DX11=OFF`

Dependencies: `libxcb`

### Pipewire
Viewing and management of pipewire nodes.

To disable: `-DSERVICE_PIPEWIRE=OFF`

Dependencies: `libpipewire`

### StatusNotifier / System Tray, MPRIS, UPower, Notifications, Bluetooth, Network
DBus based services.

To disable: `-DSERVICE_STATUS_NOTIFIER=OFF`, `-DSERVICE_MPRIS=OFF`,
`-DSERVICE_UPOWER=OFF`, `-DSERVICE_NOTIFICATIONS=OFF`, `-DBLUETOOTH=OFF`,
`-DNETWORK=OFF`

Dependencies: `qt6dbus` (usually part of qt6base)

### PAM
User authentication.

To disable: `-DSERVICE_PAM=OFF`

Dependencies: `pam`

### Polkit
Polkit agents that prompt for authentication.

To disable: `-DSERVICE_POLKIT=OFF`

Dependencies: `polkit`, `glib`

### Hyprland
Hyprland integrations. Requires wayland, no extra dependencies.

To disable: `-DHYPRLAND=OFF`, or per feature `-DHYPRLAND_IPC=OFF`,
`-DHYPRLAND_GLOBAL_SHORTCUTS=OFF`, `-DHYPRLAND_FOCUS_GRAB=OFF`,
`-DHYPRLAND_SURFACE_EXTENSIONS=OFF`

### i3/Sway
i3 and Sway IPC. No dependency on wayland or x11.

To disable: `-DI3=OFF` or `-DI3_IPC=OFF`

## Building
*For developers and prospective contributors: see [CONTRIBUTING.md](CONTRIBUTING.md).*

Only `ninja` builds are tested.

```sh
cmake -GNinja -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DDISTRIBUTOR="your distribution" \
  -DGIT_REVISION="$(git rev-parse HEAD)" \
  [disable flags from above]
cmake --build build
DESTDIR=/path/to/stage cmake --install build
```

Features you do not supply dependencies for MUST be disabled with their flags
or the build fails. clang builds noticeably faster than gcc.
