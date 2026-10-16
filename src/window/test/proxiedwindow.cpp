#include "proxiedwindow.hpp"

#include <qcoreapplication.h>
#include <qevent.h>
#include <qpoint.h>
#include <qquickwindow.h>
#include <qtest.h>
#include <qtestcase.h>

#include "../proxywindow.hpp"

void TestProxiedWindow::contextMenuWithoutPositionAndFocusItem() { // NOLINT
	auto window = ProxyWindowBase();
	window.reload();
	auto* backing = window.backingWindow();
	QVERIFY(backing != nullptr);

	QCOMPARE(backing->activeFocusItem(), nullptr);

	QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(), QPoint(), Qt::NoModifier);
	// Without the guard this crashes inside QQuickDeliveryAgentPrivate::contextMenuTargets().
	QVERIFY(QCoreApplication::sendEvent(backing, &event));
}

QTEST_MAIN(TestProxiedWindow);

#include "proxiedwindow.moc"
