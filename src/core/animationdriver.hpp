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
#include <qtypes.h>
#include <qvarlengtharray.h>
#include <qwindow.h>

#include "frameclock.hpp"
#include "framephase.hpp"
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
	// Names a window whose frame starts with the next advance().
	static void batchFrame(QWindow* window);
	static void framesDispatched();
	// A FrameAnimation's frameTime, moved onto the display cadence the current tick was locked to.
	static double frameDelta(double frameTime);

	// Taken by the render thread before handing a frame back; incubation yields until it is released.
	static std::shared_ptr<void> holdIncubation();
	// Null while a rendered frame waits for its commit.
	static std::atomic<bool>* openIncubation();
	static std::chrono::nanoseconds incubationBudget();
	// A third of the fastest rendering window's frame, taken when its frames leave no gap.
	static std::chrono::nanoseconds forcedIncubationBudget();
	static std::chrono::nanoseconds incubationWake();

	void advance() override;
	[[nodiscard]] qint64 elapsed() const override;

signals:
	void betweenFrames();

private:
	explicit FrameAnimationDriver();
	void armFallback();
	void startFallback(qint64 now);
	void dispatchFallback();

	QChronoTimer fallback;
	QElapsedTimer timer;
	FrameClock clock;
	FrameSchedule schedule;
	FramePhase phase;
	QSet<QWindow*> trackedWindows;
	QVarLengthArray<QWindow*, 8> batch;
	qint64 fallbackDeadline = 0;
	qint64 armedDeadline = 0;
	qint64 startTime = 0;
	qint64 frameTime = 0;
	qint64 offset = 0;
	qint64 previousOffset = 0;
};

} // namespace qs::core
