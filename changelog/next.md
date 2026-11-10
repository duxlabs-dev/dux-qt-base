## Other Changes

- Added experimental Wayland rendering with partial damage, enabled by the `RenderControl` pragma.
- `SystemTrayItem.activate()` now forwards Wayland activation tokens to applications.
- Added `DiskCache` pragma which loads the config from a mirror in the runtime dir to enable the qml disk cache, visible in `Component.url` and `Qt.resolvedUrl` results.
- Added `inputOnly` to windows, which attaches a single pixel scaled to the surface instead of rendering the scene under the `RenderControl` pragma.
- Added `Quickshell.frameDelta()` and `Quickshell.springFrameCoefficients()`, which move a FrameAnimation's `frameTime` onto the display cadence under the `RenderControl` pragma.
- The `RenderControl` pragma now works with Qt Quick's Vulkan backend as well as OpenGL.
- Shapes using `Shape.CurveRenderer` and text using `Text.CurveRendering` now report partial damage under the `RenderControl` pragma.

## Bug Fixes

