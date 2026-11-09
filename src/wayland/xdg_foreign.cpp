#include "xdg_foreign.hpp"

#include <qstring.h>
#include <qwayland-xdg-foreign-unstable-v2.h>
#include <qwaylandclientextension.h>
#include <wayland-client-protocol.h>

namespace qs::wayland::xdg_foreign {

XdgImported::XdgImported(
    ::zxdg_imported_v2* imported // NOLINT(misc-include-cleaner)
)
    : QtWayland::zxdg_imported_v2(imported) {}

XdgImported::~XdgImported() {
	if (!this->isInitialized()) return;
	this->destroy();
}

void XdgImported::setParentOf(::wl_surface* surface) {
	if (!this->isInitialized()) return;
	this->set_parent_of(surface);
}

void XdgImported::zxdg_imported_v2_destroyed() { this->destroy(); }

XdgImporter::XdgImporter(): QWaylandClientExtensionTemplate(1) { this->initialize(); }

XdgImported* XdgImporter::importToplevel(const QString& handle) {
	return new XdgImported(this->import_toplevel(handle));
}

XdgImporter* XdgImporter::instance() {
	static auto* instance = new XdgImporter(); // NOLINT
	return instance;
}

} // namespace qs::wayland::xdg_foreign
