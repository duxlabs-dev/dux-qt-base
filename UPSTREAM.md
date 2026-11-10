# upstream

Fork of quickshell, https://git.outfoxxed.me/quickshell/quickshell
(mirror: https://github.com/quickshell-mirror/quickshell), LGPL-3.0-only.

Imported 2026-11-06 from upstream master `9b0f339b` (post v0.3.2) plus two open upstream PRs:

- quickshell-mirror/quickshell#1049 `core: load configs from the vfs mirror to enable the qml disk cache` (head `97b62b85767bcdc3dc6c818e3584aca50fbf48e4`)
- quickshell-mirror/quickshell#1448 `wayland: opt-in render control with partial damage` (head `2fa7de8f`)

Cherry-picked 2026-11-07, without their changelog hunks:

- #1345 `io/socket: don't touch a socket deleted by an error handler`
- #1372 `bluetooth: resync adapter state via GetAll after InterfacesAdded`
- #1059 `bluetooth: use deleteLater instead of delete on device removal`
- #1302 `pipewire: recover default sink/source when a node is re-created with the same name`
- #1191 `service/tray: fix watcher missing items that exit during registration`
- #1407 `core: watch .js files imported from qml`
- #1335 `core/generation: re-watch a replaced file when its change is reported`
- #1097 `wayland/idle_notify: re-subscribe isIdle when the notification is replaced`
- #1325 `window: consume context menu events with no position on focusless windows`
- #1095 `wayland/screencopy: fix use-after-free crash from the transform query's wl_output sharing Qt's listener`
- #1278 `service/pam: fail cleanly when the subprocess cannot fork`
- #909 `wayland/background-effect: attach effect surface when manager global appears late`
- #890 `launch: set the application version so GetServerInformation reports one`
- #1219 `io/process: let qmllint read qprocess enums`

Reworked rather than cherry-picked:

- #899 `wayland/window: add WaylandWindow attached object for overriding App ID`: its app id code backs `FloatingWindow.appId` instead of a `WaylandWindow` attached type, so the property also works on X11.

Drop each from this list once it lands upstream.

## modified files

This section is the modification notice required by GPL section 5(a). Update
it when the fork diverges further.

- `CMakeLists.txt`: project renamed to dux-qt-base, position independent
  code on, `BUILD_EXECUTABLE` option, jemalloc and crash handler default
  off, bug report URL, dropped the qs symlink and desktop/icon installs.
- `src/CMakeLists.txt`: `quickshell` target is a shared library named
  dux-qt-base with SOVERSION 0, installs header and pkg-config file, optional
  standalone executable.
- `src/capi.cpp`, `include/dux-qt-base/dux_qt_base.h`,
  `dux-qt-base.pc.in`: new, C entry points and pkg-config metadata.
- `src/main.cpp`: calls `dqb_main`.
- `src/build/build.hpp.in`: version macros read `PROJECT_VERSION`.
- `src/crash/interface.cpp`: crash window names dux-qt-base.
- `src/window/floatingwindow.*`: `appId` and `foreignParent` properties
  and a platform hook. `src/wayland/floatingwindow.*`,
  `src/wayland/xdg_foreign.*`, `src/x11/floatingwindow.*`: new, the Wayland
  (xdg_toplevel app id, xdg-foreign-v2) and X11 (`WM_CLASS`,
  `WM_TRANSIENT_FOR`) sides. `src/wayland/CMakeLists.txt`,
  `src/wayland/init.cpp`, `src/x11/CMakeLists.txt`, `src/x11/init.cpp`
  wire them up.
- `assets/`: removed.
- `README.md`, `BUILD.md`, `Justfile`, `default.nix`, `.github/`: fork
  documentation, packaging, and CI.
- `src/core/animationdriver.*`, `frameclock.hpp`, `frameschedule.hpp`,
  `framephase.hpp`: batched animation sampling, per-window fallback deadlines,
  predicted frame starts, and animation time locked to each window's frame
  cadence.
- `src/core/incubator.*`: with render control, incubation runs between
  predicted frames and yields while a rendered frame waits for its commit.
- `src/core/qmlglobal.*`, `spring.hpp`: analytical spring coefficients
  exposed through `Quickshell.springCoefficients()`, plus
  `Quickshell.frameDelta()` and `Quickshell.springFrameCoefficients()`.
- `src/wayland/rendercontrol.*`, `renderthread.*`, `presentation.*`,
  `CMakeLists.txt`: per-window render threads with a per-frame animator driver,
  bounded buffer allocation during resize, a new scene after device loss, and
  optional compositor presentation timing.
- `src/window/rendercontrol.*`: the render control and its animation driver
  are only enabled where the platform can create one, on OpenGL or Vulkan.
- `src/services/notifications/`: activation tokens for actions, sent even if
  the requesting window is destroyed first; image rows padded to the rowstride.
- `src/window/damage.*`, `curvestroke.*`: scene graph callbacks run on the
  render thread; GUI invalidation uses an atomic flag; curve renderer shapes
  and text are bounded instead of damaging the whole window.
- `src/wayland/buffer/*`, `src/wayland/renderthread.*`: render control on
  Vulkan. Buffers requested as render targets are restricted to modifiers the
  device can draw into, imported as color attachments, and released to the
  foreign queue family after each frame. `src/window/proxywindow.cpp` and
  `src/wayland/session_lock.cpp` request `VK_EXT_queue_family_foreign`.

## syncing

The `upstream` remote points at the mirror. History is upstream's, with the
open PRs merged from `upstream-pr-<n>` branches and fork commits on top.

```sh
git fetch upstream
git merge upstream/master
```

Expect conflicts only in the files listed above.
