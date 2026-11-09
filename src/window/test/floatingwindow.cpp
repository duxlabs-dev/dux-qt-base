#include "floatingwindow.hpp"

#include <qlatin1stringview.h>
#include <qstring.h>
#include <qtest.h>
#include <qtestcase.h>

#include "../floatingwindow.hpp"
#include "../proxywindow.hpp"

using namespace Qt::StringLiterals;

void TestFloatingWindow::foreignParentHandle() { // NOLINT
	auto window = ProxyFloatingWindow();

	QCOMPARE(window.foreignParentHandle("wayland:"_L1), QString());

	window.bForeignParent = u"wayland:6f8a3e0c"_s;
	QCOMPARE(window.foreignParentHandle("wayland:"_L1), u"6f8a3e0c"_s);
	QCOMPARE(window.foreignParentHandle("x11:"_L1), QString());

	window.bForeignParent = u"x11:3a00004"_s;
	QCOMPARE(window.foreignParentHandle("x11:"_L1), u"3a00004"_s);
	QCOMPARE(window.foreignParentHandle("wayland:"_L1), QString());

	auto parent = ProxyWindowBase();
	window.setParentWindow(&parent);
	QCOMPARE(window.foreignParentHandle("x11:"_L1), QString());
}

QTEST_MAIN(TestFloatingWindow);

#include "floatingwindow.moc"
