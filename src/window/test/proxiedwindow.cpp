#include "proxiedwindow.hpp"

#include <qcoreapplication.h>
#include <qevent.h>
#include <qeventloop.h>
#include <qnamespace.h>
#include <qpoint.h>
#include <qquickitem.h>
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

// Mimics the production reload flow: every QML generation creates new
// ProxyWindowBase objects that adopt the previous generation's backing window via
// reload(oldInstance) -> retrieveWindow() -> disownWindow(). A changed window color
// (dms theming flips the window alpha between generations) makes ensureQWindow()
// replace the adopted window. The old generation proxy is deleted while events keep
// flowing, and grabWindow() inserts shared QQuickPixmapCache entries referencing the
// current window — the exact state whose corruption was observed in dms coredumps
// (SIGSEGV: shrinkCache <- ~QQuickWindow <- ~ProxiedWindow). Run under ASAN.
void TestProxiedWindow::formatFlipRecreation() {
	auto* current = new ProxyWindowBase();
	current->setVisible(true);
	current->reload();
	QVERIFY(current->backingWindow() != nullptr);
	auto* firstWindow = current->backingWindow();

	for (int i = 0; i < 200; ++i) {
		auto* next = new ProxyWindowBase();
		next->setVisible(true);
		// i == 0 flips alpha 255 -> 254 against the default (opaque) color, so the
		// very first generation must already replace the adopted window; the test
		// would be vacuous otherwise.
		next->setColor(QColor(255, 0, 0, i % 2 == 0 ? 254 : 255));
		next->reload(current);
		QCoreApplication::processEvents(QEventLoop::AllEvents);
		QVERIFY(next->backingWindow() != nullptr);
		if (i == 0) QVERIFY(next->backingWindow() != firstWindow);
		next->backingWindow()->grabWindow();

		delete current; // previous generation dies; its window was transferred above
		current = next;
		QCoreApplication::processEvents(QEventLoop::AllEvents);
	}

	// drain pending DeferredDelete events from the last generations
	for (int i = 0; i < 10; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents);
	delete current;
}


QTEST_MAIN(TestProxiedWindow);

#include "proxiedwindow.moc"
