#include "floatingwindow.hpp"

#include <qbytearray.h>
#include <qguiapplication.h>
#include <qlatin1stringview.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qstring.h>
#include <qwindow.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "../window/floatingwindow.hpp"
#include "../window/proxywindow.hpp"
#include "util.hpp"

using namespace Qt::StringLiterals;

namespace {

void setWindowType(QWindow* window, Qt::WindowType type) {
	window->setFlags((window->flags() & ~Qt::WindowType_Mask) | type);
}

} // namespace

XFloatingWindow::XFloatingWindow(ProxyFloatingWindow* window)
    : QObject(window)
    , proxyWindow(window) {
	QObject::connect(
	    window,
	    &ProxyWindowBase::windowConnected,
	    this,
	    &XFloatingWindow::onWindowConnected
	);

	QObject::connect(window, &ProxyFloatingWindow::appIdChanged, this, &XFloatingWindow::applyAppId);

	QObject::connect(
	    window,
	    &ProxyFloatingWindow::foreignParentChanged,
	    this,
	    &XFloatingWindow::onForeignParentChanged
	);

	if (window->backingWindow()) {
		this->onWindowConnected();
	}
}

void XFloatingWindow::onWindowConnected() {
	this->mWindow = this->proxyWindow->backingWindow();

	QObject::connect(
	    this->mWindow,
	    &QWindow::visibleChanged,
	    this,
	    &XFloatingWindow::onWindowVisibleChanged
	);

	this->onWindowVisibleChanged();
}

// visibleChanged is emitted before the platform window is shown, so both land before the map.
void XFloatingWindow::onWindowVisibleChanged() {
	if (!this->mWindow->isVisible()) return;
	if (!this->mWindow->handle()) this->mWindow->create();

	this->applyAppId();
	this->applyForeignParent();
}

// Qt writes WM_TRANSIENT_FOR only when it maps the window.
void XFloatingWindow::onForeignParentChanged() {
	if (!this->mWindow || !this->mWindow->isVisible() || !this->mWindow->handle()) return;
	this->applyForeignParent();

	auto window = static_cast<xcb_window_t>(this->mWindow->winId());
	if (this->foreignParent) {
		auto parent = static_cast<xcb_window_t>(this->foreignParent->winId());
		xcb_change_property(
		    x11Connection(),
		    XCB_PROP_MODE_REPLACE,
		    window,
		    XCB_ATOM_WM_TRANSIENT_FOR,
		    XCB_ATOM_WINDOW,
		    32,
		    1,
		    &parent
		);
	} else {
		xcb_delete_property(x11Connection(), window, XCB_ATOM_WM_TRANSIENT_FOR);
	}

	xcb_flush(x11Connection());
}

void XFloatingWindow::applyAppId() {
	if (!this->mWindow || !this->mWindow->handle()) return;

	auto appId = this->proxyWindow->bAppId.value();
	if (appId.isEmpty()) appId = QGuiApplication::desktopFileName();
	if (appId.isEmpty()) return;

	auto wmClass = appId.toUtf8() + '\0' + appId.toUtf8() + '\0';

	xcb_change_property(
	    x11Connection(),
	    XCB_PROP_MODE_REPLACE,
	    static_cast<xcb_window_t>(this->mWindow->winId()),
	    XCB_ATOM_WM_CLASS,
	    XCB_ATOM_STRING,
	    8,
	    wmClass.size(),
	    wmClass.constData()
	);

	xcb_flush(x11Connection());
}

// Qt only writes WM_TRANSIENT_FOR for dialog type windows.
void XFloatingWindow::applyForeignParent() {
	auto ok = false;
	auto xid = this->proxyWindow->foreignParentHandle("x11:"_L1).toULongLong(&ok, 16);

	if (!ok) {
		if (!this->foreignParent) return;

		if (this->mWindow->transientParent() == this->foreignParent.get()) {
			this->mWindow->setTransientParent(nullptr);
			setWindowType(this->mWindow, this->typeBeforeDialog);
		}

		this->foreignParent = nullptr;
		return;
	}

	if (!this->foreignParent) this->typeBeforeDialog = this->mWindow->type();

	this->foreignParent.reset(QWindow::fromWinId(xid));
	this->mWindow->setTransientParent(this->foreignParent.get());
	setWindowType(this->mWindow, Qt::Dialog);
}
