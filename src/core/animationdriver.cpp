#include "animationdriver.hpp"
#include <algorithm>
#include <chrono>

#include <private/qsgrenderloop_p.h>
#include <qabstractanimation.h>
#include <qchronotimer.h>
#include <qguiapplication.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qscreen.h>

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

bool FrameAnimationDriver::advanceFrame() {
	if (!DRIVER || !DRIVER->isRunning()) return false;
	DRIVER->advance();
	return DRIVER->isRunning();
}

FrameAnimationDriver::FrameAnimationDriver() {
	this->fallback.setTimerType(Qt::PreciseTimer);

	// clang-format off
	QObject::connect(&this->fallback, &QChronoTimer::timeout, this, &FrameAnimationDriver::advance);
	QObject::connect(this, &QAnimationDriver::started, this, [this] { this->fallback.start(); });
	QObject::connect(this, &QAnimationDriver::stopped, &this->fallback, &QChronoTimer::stop);
	// clang-format on

	auto* app = static_cast<QGuiApplication*>(QGuiApplication::instance()); // NOLINT
	QObject::connect(app, &QGuiApplication::screenAdded, this, &FrameAnimationDriver::onScreenAdded);
	QObject::connect(
	    app,
	    &QGuiApplication::screenRemoved,
	    this,
	    &FrameAnimationDriver::updateInterval
	);
	for (auto* screen: QGuiApplication::screens()) this->onScreenAdded(screen);
}

// A rendering window restarts the fallback every frame, so it only fires when nothing visible moves.
void FrameAnimationDriver::advance() {
	this->fallback.start();
	QAnimationDriver::advance();
}

void FrameAnimationDriver::onScreenAdded(QScreen* screen) {
	QObject::connect(
	    screen,
	    &QScreen::refreshRateChanged,
	    this,
	    &FrameAnimationDriver::updateInterval
	);
	this->updateInterval();
}

// Two periods of the fastest screen: a window rendering at any refresh rate wins the race.
void FrameAnimationDriver::updateInterval() {
	auto refreshRate = 60.0;
	for (auto* screen: QGuiApplication::screens()) {
		refreshRate = std::max(refreshRate, screen->refreshRate());
	}

	const auto period = std::chrono::duration<double>(2 / refreshRate);
	this->fallback.setInterval(std::chrono::round<std::chrono::nanoseconds>(period));
}

} // namespace qs::core
