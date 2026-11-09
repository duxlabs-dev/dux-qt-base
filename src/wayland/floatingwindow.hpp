#pragma once

#include <memory>

#include <qobject.h>
#include <qtmetamacros.h>
#include <qwindow.h>

#include "../window/floatingwindow.hpp"
#include "xdg_foreign.hpp"

namespace QtWaylandClient {
class QWaylandWindow;
}

namespace qs::wayland {

class WaylandFloatingWindow: public QObject {
	Q_OBJECT;

public:
	explicit WaylandFloatingWindow(ProxyFloatingWindow* window);

private slots:
	void onWindowConnected();
	void onWindowVisibleChanged();
	void onWaylandWindowDestroyed();
	void onSurfaceRoleCreated();
	void onForeignParentChanged();
	void applyAppId();

private:
	void applyForeignParent();

	ProxyFloatingWindow* proxyWindow;
	QWindow* mWindow = nullptr;
	QtWaylandClient::QWaylandWindow* mWaylandWindow = nullptr;
	std::unique_ptr<xdg_foreign::XdgImported> imported;
};

} // namespace qs::wayland
