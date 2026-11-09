#include "animationdriver.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>

#include <private/qsgrenderloop_p.h>
#include <qabstractanimation.h>
#include <qchronotimer.h>
#include <qguiapplication.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qscreen.h>
#include <qtmetamacros.h>
#include <qwindow.h>

namespace qs::core {

namespace {
FrameAnimationDriver* DRIVER = nullptr; // NOLINT
std::atomic<bool> INCUBATING = false;   // NOLINT
std::atomic<int> UNCOMMITTED = 0;       // NOLINT

std::int64_t periodOf(double refreshRate) {
	const auto rate = std::isfinite(refreshRate) && refreshRate > 0 ? refreshRate : 60;
	return static_cast<std::int64_t>(1e9 / rate);
}

std::int64_t periodOf(QWindow* window) {
	return periodOf(window->screen() ? window->screen()->refreshRate() : 60);
}

std::int64_t fastestPeriod() {
	auto period = periodOf(60);
	for (auto* screen: QGuiApplication::screens()) {
		period = std::min(period, periodOf(screen->refreshRate()));
	}
	return period;
}
} // namespace

void FrameAnimationDriver::install() {
	if (DRIVER) return;

	// Only one driver can be installed; the loop's must leave first, which also stops its timer.
	if (auto* loopDriver = QSGRenderLoop::instance()->animationDriver()) loopDriver->uninstall();

	DRIVER = new FrameAnimationDriver();
	DRIVER->QAnimationDriver::install();
}

FrameAnimationDriver* FrameAnimationDriver::instance() { return DRIVER; }

bool FrameAnimationDriver::frameStarted(QWindow* window) {
	if (!DRIVER) return false;
	DRIVER->clock.forget(window);
	DRIVER->schedule.started(window, DRIVER->elapsed.nsecsElapsed(), periodOf(window));
	DRIVER->armFallback();
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
	DRIVER->schedule.forget(window);
	DRIVER->armFallback();
}

void FrameAnimationDriver::framesDispatched() {
	if (DRIVER) emit DRIVER->betweenFrames();
}

std::shared_ptr<void> FrameAnimationDriver::holdIncubation() {
	UNCOMMITTED.fetch_add(1);
	INCUBATING.store(false);
	return {nullptr, [](void* /*unused*/) {
		        if (UNCOMMITTED.fetch_sub(1) == 1) framesDispatched();
	        }};
}

std::atomic<bool>* FrameAnimationDriver::openIncubation() {
	// Store first: a hold taken after the check still clears the flag.
	INCUBATING.store(true);
	if (UNCOMMITTED.load() != 0) return nullptr;
	return &INCUBATING;
}

std::chrono::nanoseconds FrameAnimationDriver::incubationBudget() {
	if (!DRIVER) return {};
	const auto now = DRIVER->elapsed.nsecsElapsed();
	return std::chrono::nanoseconds(DRIVER->schedule.deadline(now, fastestPeriod()) - now);
}

std::chrono::nanoseconds FrameAnimationDriver::incubationWake() {
	if (!DRIVER) return {};
	const auto expiry = DRIVER->schedule.expiry();
	if (expiry == FrameSchedule::NONE) return {};
	return std::chrono::nanoseconds(
	    std::max<std::int64_t>(0, expiry - DRIVER->elapsed.nsecsElapsed())
	);
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
	if (!this->isRunning()) return;
	if (this->clock.sample(this->elapsed.nsecsElapsed())) QAnimationDriver::advance();
	this->armFallback();
}

} // namespace qs::core
