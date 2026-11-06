# upstream

Fork of quickshell, https://git.outfoxxed.me/quickshell/quickshell
(mirror: https://github.com/quickshell-mirror/quickshell), LGPL-3.0-only.

Imported 2026-11-06 from upstream master `9b0f339b` (post v0.3.2) plus two open upstream PRs:

- quickshell-mirror/quickshell#1049 `core: load configs from the vfs mirror to enable the qml disk cache` (head `97b62b85767bcdc3dc6c818e3584aca50fbf48e4`)
- quickshell-mirror/quickshell#1448 `wayland: opt-in render control with partial damage` (head `2fa7de8f`)

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
- `assets/`: removed.
- `README.md`, `BUILD.md`, `Justfile`, `default.nix`, `.github/`,
  `distro/`: fork documentation, packaging, and CI.

## syncing

The `upstream` remote points at the mirror. History is upstream's, with the
open PRs merged from `upstream-pr-<n>` branches and fork commits on top.

```sh
git fetch upstream
git merge upstream/master
```

Expect conflicts only in the files listed above.
