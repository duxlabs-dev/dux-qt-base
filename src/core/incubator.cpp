#include "incubator.hpp"
#include <chrono>

#include <private/qqmlengine_p.h>
#include <private/qqmlincubator_p.h>
#include <private/qqmlvme_p.h>
#include <private/qsgrenderloop_p.h>
#include <qabstractanimation.h>
#include <qdeadlinetimer.h>
#include <qguiapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qminmax.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qqmlincubator.h>
#include <qscreen.h>
#include <qtmetamacros.h>

#include "animationdriver.hpp"
#include "logcat.hpp"

QS_LOGGING_CATEGORY(logIncubator, "quickshell.incubator", QtWarningMsg);

namespace {
constexpr auto MIN_SLICE = std::chrono::microseconds(100);
constexpr int MAX_DEFERRED_SLICES = 4;
} // namespace

void QsQmlIncubator::statusChanged(QQmlIncubator::Status status) {
	switch (status) {
	case QQmlIncubator::Ready: emit this->completed(); break;
	case QQmlIncubator::Error: emit this->failed(); break;
	default: break;
	}
}

void QsIncubationController::initLoop() {
	auto* app = static_cast<QGuiApplication*>(QGuiApplication::instance()); // NOLINT
	this->renderLoop = QSGRenderLoop::instance();

	QObject::connect(
	    app,
	    &QGuiApplication::screenAdded,
	    this,
	    &QsIncubationController::updateIncubationTime
	);

	QObject::connect(
	    app,
	    &QGuiApplication::screenRemoved,
	    this,
	    &QsIncubationController::updateIncubationTime
	);

	this->updateIncubationTime();

	QObject::connect(
	    this->renderLoop,
	    &QSGRenderLoop::timeToIncubate,
	    this,
	    &QsIncubationController::incubate
	);

	if (auto* frameDriver = qs::core::FrameAnimationDriver::instance()) {
		this->wake.setTimerType(Qt::PreciseTimer);
		this->wake.setSingleShot(true);
		// clang-format off
		QObject::connect(&this->wake, &QChronoTimer::timeout, this, &QsIncubationController::incubate);
		QObject::connect(frameDriver, &qs::core::FrameAnimationDriver::betweenFrames, this, [this] {
			if (!this->incubatingObjectCount()) return;
			QMetaObject::invokeMethod(this, &QsIncubationController::incubate, Qt::QueuedConnection);
		});
		// clang-format on
		return;
	}

	auto* animationDriver = this->renderLoop->animationDriver();
	if (animationDriver) {
		QObject::connect(
		    animationDriver,
		    &QAnimationDriver::stopped,
		    this,
		    &QsIncubationController::animationStopped
		);
	} else {
		qCInfo(logIncubator) << "Render loop does not have animation driver, animationStopped cannot "
		                        "be used to trigger incubation.";
	}
}

void QsIncubationController::setIncubationMode(bool render) {
	if (render == this->followRenderloop) return;
	this->followRenderloop = render;

	if (render) {
		qCDebug(logIncubator) << "Incubation mode changed: render loop driven";
	} else {
		qCDebug(logIncubator) << "Incubation mode changed: event loop driven";
	}

	if (!render && this->incubatingObjectCount()) this->incubateLater();
}

void QsIncubationController::timerEvent(QTimerEvent* /*event*/) {
	this->killTimer(this->timerId);
	this->timerId = 0;
	this->incubate();
}

void QsIncubationController::incubateLater() {
	if (this->followRenderloop) {
		if (this->timerId != 0) {
			this->killTimer(this->timerId);
			this->timerId = 0;
		}

		// Incubate again at the end of the event processing queue
		QMetaObject::invokeMethod(this, &QsIncubationController::incubate, Qt::QueuedConnection);
	} else if (this->timerId == 0) {
		// Wait for a while before processing the next batch. Using a
		// timer to avoid starvation of system events.
		this->timerId = this->startTimer(this->incubationTime);
	}
}

void QsIncubationController::incubate() {
	if (qs::core::FrameAnimationDriver::instance()) {
		this->incubateBetweenFrames();
		return;
	}

	if ((!this->followRenderloop || this->renderLoop) && this->incubatingObjectCount()) {
		if (!this->followRenderloop) {
			this->incubateFor(10);
			if (this->incubatingObjectCount()) this->incubateLater();
		} else if (this->renderLoop->interleaveIncubation()) {
			this->incubateFor(this->incubationTime);
		} else {
			this->incubateFor(this->incubationTime * 2);
			if (this->incubatingObjectCount()) this->incubateLater();
		}
	}
}

void QsIncubationController::animationStopped() { this->incubate(); }

void QsIncubationController::incubateBetweenFrames() {
	if (!this->incubatingObjectCount()) {
		this->deferredSlices = 0;
		return;
	}

	std::chrono::nanoseconds budget = qs::core::FrameAnimationDriver::incubationBudget();
	if (budget < MIN_SLICE) {
		// A window rendering every refresh can leave no gap; slice anyway so loaders still finish.
		if (++this->deferredSlices < MAX_DEFERRED_SLICES) {
			this->wake.setInterval(qs::core::FrameAnimationDriver::incubationWake());
			this->wake.start();
			return;
		}
		budget = qs::core::FrameAnimationDriver::forcedIncubationBudget();
	}

	// The released hold dispatches the next slice.
	auto* run = qs::core::FrameAnimationDriver::openIncubation();
	if (!run) return;
	this->deferredSlices = 0;

	QQmlInstantiationInterrupt interrupt(run, QDeadlineTimer(budget, Qt::PreciseTimer));
	do {
		auto* engine = QQmlEnginePrivate::get(this->engine());
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
		static_cast<QQmlIncubatorPrivate*>(engine->incubatorList.first())->incubate(interrupt);
	} while (this->incubatingObjectCount() && !interrupt.shouldInterrupt());

	if (this->incubatingObjectCount()) {
		QMetaObject::invokeMethod(this, &QsIncubationController::incubate, Qt::QueuedConnection);
	}
}

void QsIncubationController::incubatingObjectCountChanged(int count) {
	if (count && qs::core::FrameAnimationDriver::instance()) {
		QMetaObject::invokeMethod(this, &QsIncubationController::incubate, Qt::QueuedConnection);
		return;
	}

	if (count
	    && (!this->followRenderloop
	        || (this->renderLoop && !this->renderLoop->interleaveIncubation())))
	{
		this->incubateLater();
	}
}

void QsIncubationController::updateIncubationTime() {
	auto* screen = QGuiApplication::primaryScreen();
	if (!screen) return;

	// 1/3 frame on primary screen
	this->incubationTime = qMax(1, static_cast<int>(1000 / screen->refreshRate() / 3));
}
