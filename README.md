# dux-qt-base

The quickshell QML runtime as a shared library. Dux apps (dms, dcal, dfiles)
link it through cgo and run the UI by re-executing their own binary, so no
`qs` executable is needed on the system.

Fork of [quickshell](https://quickshell.org) by outfoxxed. Imported from
upstream commit `97b62b85767bcdc3dc6c818e3584aca50fbf48e4`. The QML modules (`Quickshell`, `Quickshell.Io`,
`Quickshell.Wayland`, `Quickshell.Services.*`, ...), the pragmas, the config
and IPC CLI, the run and state directories, and plugin loading are unchanged,
so configs written for quickshell keep working.

## What changed

- `libdux-qt-base.so.0` replaces the `quickshell` executable. The CMake
  target is still named `quickshell` so every upstream module links into it.
- C entry points in `include/dux-qt-base/dux_qt_base.h`: `dqb_main(argc, argv)`
  runs the quickshell command line and returns its exit code, `dqb_version()`
  and `dqb_git_revision()` report the build.
- `dux-qt-base.pc` for pkg-config consumers.
- jemalloc and the crash handler default off. The host process supervises the
  UI and owns crash reporting.
- No `qs` symlink, desktop file, icon, or on-disk QML install.
- `-DBUILD_EXECUTABLE=ON` still builds a standalone `dux-qt-base` binary
  for development.

## Build

```sh
cmake -GNinja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DDISTRIBUTOR=yourname
cmake --build build
cmake --install build
```

Dependencies are listed in [BUILD.md](BUILD.md).

## Use from Go

```go
// #cgo pkg-config: dux-qt-base
// #include <dux-qt-base/dux_qt_base.h>
import "C"
```

The shared binding lives in `github.com/duxlabs-dev/dux-go/qtbase`.

Apps vendor this repo as a submodule and link it statically instead of
installing the shared library: build with cmake, then
`scripts/cgo-link-flags.py <build dir> <out>/dux-qt-base.pc` writes a
pkg-config file whose `Libs` repeats the library's own link line (archives,
init objects, system libs) so `go build` with
`PKG_CONFIG_PATH=<out>` produces one binary with no `libdux-qt-base.so`
dependency. cgo rejects `-Wl,--export-dynamic-symbol`, which the wayland
null-proxy guard needs, so set `CGO_LDFLAGS_ALLOW='-Wl,--export-dynamic-symbol=.*'`.

## License

LGPL-3.0-only, see [LICENSE](LICENSE) and [LICENSE-GPL](LICENSE-GPL).
Modifications relative to upstream are listed in [UPSTREAM.md](UPSTREAM.md).
Programs that link this library must ship both license files and a notice
that the library is used, per LGPL section 4.
