#pragma once

#include <memory>

#include <qnamespace.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <qwindow.h>

#include "../window/floatingwindow.hpp"

class XFloatingWindow: public QObject {
	Q_OBJECT;

public:
	explicit XFloatingWindow(ProxyFloatingWindow* window);

private slots:
	void onWindowConnected();
	void onWindowVisibleChanged();
	void onForeignParentChanged();
	void applyAppId();

private:
	void applyForeignParent();

	ProxyFloatingWindow* proxyWindow;
	QWindow* mWindow = nullptr;
	std::unique_ptr<QWindow> foreignParent;
	Qt::WindowType typeBeforeDialog = Qt::Window;
};
