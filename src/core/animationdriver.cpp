#include "animationdriver.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <private/qsgrenderloop_p.h>
#include <qabstractanimation.h>
#include <qchronotimer.h>
#include <qguiapplication.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qscreen.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qwindow.h>

namespace qs::core {

namespace {
FrameAnimationDriver* DRIVER = nullptr; // NOLINT
std::atomic<bool> INCUBATING = false;   // NOLINT
std::atomic<int> UNCOMMITTED = 0;       // NOLINT
constexpr qint64 NS_PER_MS = 1000000;
constexpr std::int64_t FORCED_SLICE_DIVISOR = 3;

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
	DRIVER->schedule.started(window, DRIVER->timer.nsecsElapsed(), periodOf(window));
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
	DRIVER->clock.request(window, DRIVER->timer.nsecsElapsed(), rate);
	DRIVER->armFallback();
}

void FrameAnimationDriver::cancelFrame(QWindow* window) {
	if (!DRIVER) return;
	DRIVER->clock.forget(window);
	DRIVER->schedule.forget(window);
	DRIVER->phase.forget(window);
	DRIVER->batch.removeAll(window);
	DRIVER->armFallback();
}

void FrameAnimationDriver::batchFrame(QWindow* window) {
	if (DRIVER && DRIVER->isRunning()) DRIVER->batch.append(window);
}

double FrameAnimationDriver::frameDelta(double frameTime) {
	if (!DRIVER || !DRIVER->isRunning()) return frameTime;
	const auto correction = static_cast<double>(DRIVER->offset - DRIVER->previousOffset) / 1e9;
	return std::max(0.0, frameTime + correction);
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
	const auto now = DRIVER->timer.nsecsElapsed();
	return std::chrono::nanoseconds(DRIVER->schedule.deadline(now, fastestPeriod()) - now);
}

std::chrono::nanoseconds FrameAnimationDriver::forcedIncubationBudget() {
	if (!DRIVER) return {};
	const auto period = DRIVER->schedule.shortestPeriod(fastestPeriod());
	return std::chrono::nanoseconds(period / FORCED_SLICE_DIVISOR);
}

std::chrono::nanoseconds FrameAnimationDriver::incubationWake() {
	if (!DRIVER) return {};
	const auto expiry = DRIVER->schedule.expiry();
	if (expiry == FrameSchedule::NONE) return {};
	return std::chrono::nanoseconds(std::max<std::int64_t>(0, expiry - DRIVER->timer.nsecsElapsed()));
}

FrameAnimationDriver::FrameAnimationDriver() {
	this->timer.start();
	this->fallback.setTimerType(Qt::PreciseTimer);
	this->fallback.setSingleShot(true);
	QObject::connect(
	    &this->fallback,
	    &QChronoTimer::timeout,
	    this,
	    &FrameAnimationDriver::dispatchFallback
	);
	QObject::connect(this, &QAnimationDriver::started, this, [this] {
		this->clock.reset();
		this->startTime = this->timer.nsecsElapsed();
		this->frameTime = this->startTime;
		this->offset = 0;
		this->previousOffset = 0;
		this->armFallback();
	});
	QObject::connect(this, &QAnimationDriver::stopped, &this->fallback, &QChronoTimer::stop);
}

void FrameAnimationDriver::armFallback() {
	if (!this->isRunning()) return;
	const auto now = this->timer.nsecsElapsed();
	this->fallbackDeadline = now + this->clock.delay(now);
	if (this->fallback.isActive() && this->armedDeadline <= this->fallbackDeadline) return;
	this->startFallback(now);
}

void FrameAnimationDriver::startFallback(qint64 now) {
	this->fallback.stop();
	this->armedDeadline = this->fallbackDeadline;
	this->fallback.setInterval(
	    std::chrono::nanoseconds(std::max<qint64>(1, this->fallbackDeadline - now))
	);
	this->fallback.start();
}

void FrameAnimationDriver::dispatchFallback() {
	if (!this->isRunning()) return;
	const auto now = this->timer.nsecsElapsed();
	if (now < this->fallbackDeadline) {
		this->startFallback(now);
		return;
	}
	this->advance();
}

void FrameAnimationDriver::advance() {
	if (!this->isRunning()) {
		this->batch.clear();
		return;
	}

	const auto now = this->timer.nsecsElapsed();
	auto locked = now;
	auto fastest = std::numeric_limits<std::int64_t>::max();
	for (auto* window: std::as_const(this->batch)) {
		const auto period = periodOf(window);
		const auto time = this->phase.lock(window, now, period);
		if (period >= fastest) continue;
		fastest = period;
		locked = time;
	}
	this->batch.clear();

	if (this->clock.sample(now)) {
		// QUnifiedTimer skips a tick whose millisecond did not move, and a frame with nothing
		// to draw commits no buffer, which ends the frame callback chain.
		const auto next = this->startTime + (this->elapsed() + 1) * NS_PER_MS;
		this->previousOffset = this->offset;
		this->frameTime = std::max(locked, next);
		this->offset = this->frameTime - now;
		this->QAnimationDriver::advance();
	}

	this->armFallback();
}

// Constant between advances, like the vsync time of Qt's own driver.
qint64 FrameAnimationDriver::elapsed() const {
	if (!this->isRunning()) return 0;
	return (this->frameTime - this->startTime) / NS_PER_MS;
}

} // namespace qs::core
