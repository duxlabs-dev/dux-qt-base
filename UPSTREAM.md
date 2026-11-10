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

## modifications

This section is the modification notice required by GPL section 5(a).
dux-qt-base is a modified version of quickshell, changed since the import on
2026-11-06. The fork's commits and the files they touch:

```sh
git log upstream/master..
git diff --stat upstream/master...
```

## syncing

The `upstream` remote points at the mirror. History is upstream's, with the
open PRs merged from `upstream-pr-<n>` branches and fork commits on top.

```sh
git fetch upstream
git merge upstream/master
```

Expect conflicts only in files the fork changed, which
`git diff --name-only upstream/master...` lists.
