# dux-qt-base

Shared QML runtime for Dux apps. Based on
[quickshell](https://quickshell.outfoxxed.me) by outfoxxed, with changes for
loading the runtime into an app's own process. Apps such as dms use
`libdux-qt-base.so.0` to run their UI under their own name without installing
`qs`.

The upstream QML imports, config format, pragmas, IPC commands, runtime paths
and plugin support are retained. Use the quickshell docs for those APIs.
The additions below are specific to this fork.

The upstream base is `9b0f339b`, after v0.3.2. See [UPSTREAM.md](UPSTREAM.md)
for the included patches and modification history.

## Build

Requires Qt 6.8 or later. See [BUILD.md](BUILD.md) for dependencies and
packaging details.

```sh
cmake -GNinja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DDISTRIBUTOR=yourname
cmake --build build
cmake --install build
```

Installs the shared library, C header and `dux-qt-base.pc`. QML modules are
built into the library. There is no separate QML install, desktop entry, icon
or `qs` symlink. The internal CMake target is named `quickshell`.

jemalloc and the crash handler are disabled by default. The app handles UI
supervision and crash reporting. Set `-DBUILD_EXECUTABLE=ON` to also build a
standalone executable named `dux-qt-base`.

## Use

The C API is declared in `include/dux-qt-base/dux_qt_base.h`:

```c
#include <dux-qt-base/dux_qt_base.h>

int dqb_main(int argc, char** argv);
const char* dqb_version(void);
const char* dqb_git_revision(void);
```

Pass the quickshell command line to `dqb_main`, including `argv[0]`. It returns
the command's exit status. Run it on the main thread, once per process.
`dqb_version` and `dqb_git_revision` return the library's build version and
commit.

For Go, use `github.com/duxlabs-dev/dux-go/qtbase`. The package starts another
instance of the app with the `qs` subcommand, then loads the library using
purego. It requires no cgo. Other commands do not load Qt.

Library lookup order:

1. An embedded library extracted by the app.
2. `libdux-qt-base.so.0` in the executable's directory.
3. `../lib/<binary name>/libdux-qt-base.so.0`, relative to that directory.
4. `libdux-qt-base.so.0` through the system loader.

## QML additions

- Icon lookup: `Quickshell.iconTheme`, `Quickshell.iconIndexRevision` and
  `Quickshell.iconFile()`. A background task indexes the theme.
  `iconFile()` reads that index without filesystem access.
- String storage: `Quickshell.createStringMap()` stores entries outside the
  JavaScript heap.
- Animation timing: `Quickshell.frameDelta()`, `Quickshell.springCoefficients()`
  and `Quickshell.springFrameCoefficients()`.
- Window properties: `FloatingWindow.appId`, `FloatingWindow.foreignParent`
  and `inputOnly`.
- Layer focus: `WlrLayershell.focusOnMap` and
  `WlrLayershell.releaseKeyboardFocus()`.

`inputOnly` and the layer focus additions require `RenderControl`.
Without it, `frameDelta()` returns the supplied frame time unchanged.

The notification server sends `ActivationToken` before `ActionInvoked`.

Add `//@ pragma DiskCache` to load a config through the VFS mirror and enable
the QML disk cache. The runtime also frees unused render buffers, respects
requested image sizes and delays creating the preprocessing engine until it
is needed. On glibc, startup sets the malloc mmap and trim thresholds unless
they have been configured through the environment.

## Rendering

Add `//@ pragma RenderControl` for offscreen rendering with partial damage
on OpenGL or Vulkan. This requires `WAYLAND_RENDER_CONTROL`, which is enabled
in the default build.

Each window gets a render thread and graphics device. To render on the GUI
thread, set `QS_RENDER_THREAD=0`. Hiding a window releases its surface buffers.
Windows with `inputOnly` use a transparent pixel scaled to the window size.

For frame timing logs, set:

```sh
export QS_FRAME_TRACE=1
export QT_LOGGING_RULES='quickshell.wayland.rendercontrol.frame.debug=true;quickshell.wayland.rendercontrol.presentation.debug=true'
```

The frame log records CPU time for polish, sync and rendering, plus buffer
allocations. The presentation log records compositor feedback, refresh
intervals and time since the frame request and the window's last input event.
Match entries by frame serial. GPU execution time is not measured.
Presentation logging requires compositor support for `wp_presentation`.

Build and contribution policies are in [HACKING.md](HACKING.md) and
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

LGPL-3.0-only. The license texts are [LICENSE](LICENSE) and
[LICENSE-GPL](LICENSE-GPL); the modification notice is in
[UPSTREAM.md](UPSTREAM.md). When distributing an app linked to this library,
include both licenses and a notice of its use under the LGPL. See section 4
of [LICENSE](LICENSE) for the full terms.
