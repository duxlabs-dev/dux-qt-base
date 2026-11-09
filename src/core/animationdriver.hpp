#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include <qabstractanimation.h>
#include <qchronotimer.h>
#include <qelapsedtimer.h>
#include <qobject.h>
#include <qset.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qwindow.h>

#include "frameclock.hpp"
#include "frameschedule.hpp"

namespace qs::core {

class FrameAnimationDriver: public QAnimationDriver {
	Q_OBJECT;

public:
	~FrameAnimationDriver() override = default;
	Q_DISABLE_COPY_MOVE(FrameAnimationDriver);

	static void install();
	static FrameAnimationDriver* instance();
	static bool frameStarted(QWindow* window);
	static void requestFrame(QWindow* window);
	static void cancelFrame(QWindow* window);
	static void framesDispatched();

	// Taken by the render thread before handing a frame back; incubation yields until it is released.
	static std::shared_ptr<void> holdIncubation();
	// Null while a rendered frame waits for its commit.
	static std::atomic<bool>* openIncubation();
	static std::chrono::nanoseconds incubationBudget();
	static std::chrono::nanoseconds incubationWake();

	void advance() override;

signals:
	void betweenFrames();

private:
	explicit FrameAnimationDriver();
	void armFallback();

	QChronoTimer fallback;
	QElapsedTimer elapsed;
	FrameClock clock;
	FrameSchedule schedule;
	QSet<QWindow*> trackedWindows;
};

} // namespace qs::core
