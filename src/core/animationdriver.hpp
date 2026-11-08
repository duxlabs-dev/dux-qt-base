#pragma once

#include <qabstractanimation.h>
#include <qchronotimer.h>
#include <qelapsedtimer.h>
#include <qobject.h>
#include <qset.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qwindow.h>

#include "frameclock.hpp"

namespace qs::core {

///! Animation clock sampled when a window renders
/// Qt's render loop drives no render control window, so it falls back to a system timer that steps
/// animations by a fixed vsync off the vblank. This driver advances by wall time from each window's
/// frame instead, so every display gets exact per frame motion.
class FrameAnimationDriver: public QAnimationDriver {
	Q_OBJECT;

public:
	~FrameAnimationDriver() override = default;
	Q_DISABLE_COPY_MOVE(FrameAnimationDriver);

	static void install();
	static FrameAnimationDriver* instance();
	// Called before a window polishes, so its frame samples animation time at its own vblank.
	// Returns whether animations still run, so the window schedules the frame that samples the next tick.
	static bool advanceFrame(QWindow* window);
	static void requestFrame(QWindow* window);
	static void cancelFrame(QWindow* window);

	void advance() override;

private:
	explicit FrameAnimationDriver();
	void armFallback();

	QChronoTimer fallback;
	QElapsedTimer elapsed;
	FrameClock clock;
	QSet<QWindow*> trackedWindows;
};

} // namespace qs::core
