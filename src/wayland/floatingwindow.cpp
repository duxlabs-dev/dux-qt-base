#include "floatingwindow.hpp"
#include <memory>

#include <private/qwaylandshellsurface_p.h>
#include <private/qwaylandwindow_p.h>
#include <qguiapplication.h>
#include <qlatin1stringview.h>
#include <qlogging.h>
#include <qobject.h>
#include <qstring.h>
#include <qwindow.h>

#include "../window/floatingwindow.hpp"
#include "../window/proxywindow.hpp"
#include "xdg_foreign.hpp"

using namespace Qt::StringLiterals;
using QtWaylandClient::QWaylandWindow;

namespace qs::wayland {

WaylandFloatingWindow::WaylandFloatingWindow(ProxyFloatingWindow* window)
    : QObject(window)
    , proxyWindow(window) {
	QObject::connect(
	    window,
	    &ProxyWindowBase::windowConnected,
	    this,
	    &WaylandFloatingWindow::onWindowConnected
	);

	QObject::connect(
	    window,
	    &ProxyFloatingWindow::appIdChanged,
	    this,
	    &WaylandFloatingWindow::applyAppId
	);

	QObject::connect(
	    window,
	    &ProxyFloatingWindow::foreignParentChanged,
	    this,
	    &WaylandFloatingWindow::onForeignParentChanged
	);

	if (window->backingWindow()) {
		this->onWindowConnected();
	}
}

void WaylandFloatingWindow::onWindowConnected() {
	this->mWindow = this->proxyWindow->backingWindow();

	QObject::connect(
	    this->mWindow,
	    &QWindow::visibleChanged,
	    this,
	    &WaylandFloatingWindow::onWindowVisibleChanged
	);

	this->onWindowVisibleChanged();
}

void WaylandFloatingWindow::onWindowVisibleChanged() {
	if (this->mWindow->isVisible() && !this->mWindow->handle()) {
		this->mWindow->create();
	}

	auto* window = dynamic_cast<QWaylandWindow*>(this->mWindow->handle());
	if (window == this->mWaylandWindow) return;

	if (this->mWaylandWindow) {
		QObject::disconnect(this->mWaylandWindow, nullptr, this, nullptr);
	}

	this->mWaylandWindow = window;
	if (!window) return;

	QObject::connect(
	    window,
	    &QObject::destroyed,
	    this,
	    &WaylandFloatingWindow::onWaylandWindowDestroyed
	);

	// Fires before the initial commit, so the compositor sees both before mapping.
	QObject::connect(
	    window,
	    &QWaylandWindow::surfaceRoleCreated,
	    this,
	    &WaylandFloatingWindow::onSurfaceRoleCreated
	);

	if (window->shellSurface()) this->onSurfaceRoleCreated();
}

void WaylandFloatingWindow::onWaylandWindowDestroyed() { this->mWaylandWindow = nullptr; }

void WaylandFloatingWindow::onSurfaceRoleCreated() {
	if (!this->proxyWindow->bAppId.value().isEmpty()) this->applyAppId();
	this->applyForeignParent();
}

void WaylandFloatingWindow::onForeignParentChanged() {
	this->imported = nullptr;
	this->applyForeignParent();
}

void WaylandFloatingWindow::applyAppId() {
	if (!this->mWaylandWindow) return;

	auto* shellSurface = this->mWaylandWindow->shellSurface();
	if (!shellSurface) return;

	auto appId = this->proxyWindow->bAppId.value();
	shellSurface->setAppId(appId.isEmpty() ? QGuiApplication::desktopFileName() : appId);
}

void WaylandFloatingWindow::applyForeignParent() {
	if (!this->mWaylandWindow || !this->mWaylandWindow->shellSurface()) return;

	auto handle = this->proxyWindow->foreignParentHandle("wayland:"_L1);
	if (handle.isEmpty()) {
		this->imported = nullptr;
		return;
	}

	if (!this->imported) {
		auto* importer = xdg_foreign::XdgImporter::instance();

		if (!importer->isActive()) {
			qWarning() << "Cannot set foreignParent as xdg-foreign-unstable-v2 is not supported by the "
			              "compositor.";
			return;
		}

		this->imported = std::unique_ptr<xdg_foreign::XdgImported>(importer->importToplevel(handle));
	}

	this->imported->setParentOf(this->mWaylandWindow->surface());
}

} // namespace qs::wayland

void installFloatingWindowHook() { // NOLINT(misc-use-internal-linkage)
	ProxyFloatingWindow::setPlatformHook([](ProxyFloatingWindow* window) {
		new qs::wayland::WaylandFloatingWindow(window);
	});
}
