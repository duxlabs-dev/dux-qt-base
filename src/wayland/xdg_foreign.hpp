#pragma once

#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qwayland-xdg-foreign-unstable-v2.h>
#include <qwaylandclientextension.h>
#include <wayland-client-protocol.h>

namespace qs::wayland::xdg_foreign {

class XdgImported: public QtWayland::zxdg_imported_v2 {
public:
	explicit XdgImported(::zxdg_imported_v2* imported);
	~XdgImported() override;
	Q_DISABLE_COPY_MOVE(XdgImported);

	void setParentOf(::wl_surface* surface);

protected:
	void zxdg_imported_v2_destroyed() override;
};

class XdgImporter
    : public QWaylandClientExtensionTemplate<XdgImporter>
    , public QtWayland::zxdg_importer_v2 {
public:
	XdgImported* importToplevel(const QString& handle);

	static XdgImporter* instance();

private:
	explicit XdgImporter();
};

} // namespace qs::wayland::xdg_foreign
