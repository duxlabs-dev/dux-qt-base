#include "offscreensurface.hpp"

#include <qoffscreensurface.h>
#include <qsemaphore.h>
#include <qthread.h>
#include <qcoreapplication.h>
#include <qguiapplication.h>
#include <qmetatype.h>
#include <qobject.h>
#include <qstring.h>
#include <qtest.h>
#include <qtestcase.h>

// Pins the Qt threading contract behind the render-thread use-after-free fix
// (see "wayland/rendercontrol: create the offscreen surface on the gui thread").
//
// QQuickRenderControlPrivate::initRhi() creates the render control's offscreen
// surface on whichever thread calls QQuickRenderControl::initialize() when the
// surface is unset; QSGRhiSupport::maybeCreateOffscreenSurface() is documented
// main-thread-only. On Wayland the surface is a hidden QWindow, so creating it
// on a render thread affines that window and its platform window to the render
// thread — queued lifecycle events then outlive the objects during teardown.
//
// This test runs on any platform (offscreen included) and asserts the two
// facts the fix relies on:
//   1. a surface created on a worker thread IS affined to that thread
//      (i.e. the bug precondition exists — not creating it on the gui thread
//      would keep the use-after-free path alive), and
//   2. the same surface created on the gui thread is gui-affined and usable.

using namespace Qt::StringLiterals;

void TestOffscreenSurfaceAffinity::surfaceTakesCreatingThreadAffinity() { // NOLINT
	auto worker = QThread();
	auto workerContext = QObject();
	auto created = QSemaphore();
	auto surface = static_cast<QOffscreenSurface*>(nullptr);

	worker.setObjectName("affinity-test-worker"_L1);
	workerContext.moveToThread(&worker); // queued calls to it run on the worker
	worker.start();

	// Create the surface the way initRhi() would on a render thread: on the
	// calling thread, from inside that thread's event context. Objects are born
	// on their creating thread — that affinity is the bug precondition.
	QMetaObject::invokeMethod(
	    &workerContext,
	    [&]() {
		    surface = new QOffscreenSurface();
		    created.release();
	    },
	    Qt::QueuedConnection
	);

	for (int i = 0; i < 50 && created.available() == 0; ++i) {
		QTest::qWait(100); // process events while the worker runs
	}
	QVERIFY2(created.available() > 0, "surface creation never ran on the worker thread");
	QCOMPARE(surface->thread(), &worker); // the bug precondition: worker affinity
	worker.quit();
	QTRY_VERIFY(worker.isFinished());
	delete surface; // safe: the worker's event loop is gone
}

void TestOffscreenSurfaceAffinity::guiThreadSurfaceIsGuiAffinedAndUsable() { // NOLINT
	auto surface = QOffscreenSurface();
	surface.create();

	auto* guiThread = QCoreApplication::instance()->thread();
	QCOMPARE(surface.thread(), guiThread);
	QVERIFY(surface.isValid());
}

QTEST_MAIN(TestOffscreenSurfaceAffinity);

#include "offscreensurface.moc"
