#pragma once

#include <qscreen.h>
#include <qwayland-ext-image-capture-source-v1.h>
#include <qwayland-ext-image-copy-capture-v1.h>
#include <qwaylandclientextension.h>
#include <wayland-ext-foreign-toplevel-list-v1-client-protocol.h>
#include <wayland-ext-image-capture-source-v1-client-protocol.h>

#include "../manager.hpp"

namespace qs::wayland::screencopy::icc {

// NOLINTNEXTLINE(misc-multiple-inheritance)
class IccManager
    : public QWaylandClientExtensionTemplate<IccManager>
    , public QtWayland::ext_image_copy_capture_manager_v1 {
public:
	ScreencopyContext* createSession(::ext_image_capture_source_v1* source, bool paintCursors);

	static IccManager* instance();

private:
	explicit IccManager();
};

// NOLINTNEXTLINE(misc-multiple-inheritance)
class IccOutputSourceManager
    : public QWaylandClientExtensionTemplate<IccOutputSourceManager>
    , public QtWayland::ext_output_image_capture_source_manager_v1 {
public:
	ScreencopyContext* captureOutput(QScreen* screen, bool paintCursors);

	static IccOutputSourceManager* instance();

private:
	explicit IccOutputSourceManager();
};

// NOLINTNEXTLINE(misc-multiple-inheritance)
class IccToplevelSourceManager
    : public QWaylandClientExtensionTemplate<IccToplevelSourceManager>
    , public QtWayland::ext_foreign_toplevel_image_capture_source_manager_v1 {
public:
	ScreencopyContext* captureToplevel(::ext_foreign_toplevel_handle_v1* handle, bool paintCursors);

	static IccToplevelSourceManager* instance();

private:
	explicit IccToplevelSourceManager();
};

} // namespace qs::wayland::screencopy::icc
