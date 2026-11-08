#include "animationdriver.hpp"
#include <chrono>

#include <private/qsgrenderloop_p.h>
#include <qabstractanimation.h>
#include <qchronotimer.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qscreen.h>
#include <qwindow.h>

namespace qs::core {

namespace {
FrameAnimationDriver* DRIVER = nullptr; // NOLINT
}

void FrameAnimationDriver::install() {
	if (DRIVER) return;

	// Only one driver can be installed; the loop's must leave first, which also stops its timer.
	if (auto* loopDriver = QSGRenderLoop::instance()->animationDriver()) loopDriver->uninstall();

	DRIVER = new FrameAnimationDriver();
	DRIVER->QAnimationDriver::install();
}

FrameAnimationDriver* FrameAnimationDriver::instance() { return DRIVER; }

bool FrameAnimationDriver::advanceFrame(QWindow* window) {
	if (!DRIVER) return false;
	DRIVER->clock.forget(window);
	if (!DRIVER->isRunning()) return false;
	DRIVER->advance();
	return DRIVER->isRunning();
}

void FrameAnimationDriver::requestFrame(QWindow* window) {
	if (!DRIVER || !DRIVER->isRunning()) return;
	if (!DRIVER->trackedWindows.contains(window)) {
		DRIVER->trackedWindows.insert(window);
		QObject::connect(window, &QObject::destroyed, DRIVER, [window] {
			DRIVER->trackedWindows.remove(window);
			cancelFrame(window);
		});
	}
	const auto rate = window->screen() ? window->screen()->refreshRate() : 60;
	DRIVER->clock.request(window, DRIVER->elapsed.nsecsElapsed(), rate);
	DRIVER->armFallback();
}

void FrameAnimationDriver::cancelFrame(QWindow* window) {
	if (!DRIVER) return;
	DRIVER->clock.forget(window);
	DRIVER->armFallback();
}

FrameAnimationDriver::FrameAnimationDriver() {
	this->elapsed.start();
	this->fallback.setTimerType(Qt::PreciseTimer);
	this->fallback.setSingleShot(true);
	QObject::connect(&this->fallback, &QChronoTimer::timeout, this, &FrameAnimationDriver::advance);
	QObject::connect(this, &QAnimationDriver::started, this, [this] {
		this->clock.reset();
		this->armFallback();
	});
	QObject::connect(this, &QAnimationDriver::stopped, &this->fallback, &QChronoTimer::stop);
}

void FrameAnimationDriver::armFallback() {
	if (!this->isRunning()) return;
	this->fallback.setInterval(
	    std::chrono::nanoseconds(this->clock.delay(this->elapsed.nsecsElapsed()))
	);
	this->fallback.start();
}

void FrameAnimationDriver::advance() {
	if (this->clock.sample(this->elapsed.nsecsElapsed())) QAnimationDriver::advance();
	this->armFallback();
}

} // namespace qs::core
