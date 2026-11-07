#pragma once

#include <qabstractanimation.h>
#include <qchronotimer.h>
#include <qobject.h>
#include <qscreen.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>

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
	static bool advanceFrame();

	void advance() override;

private:
	explicit FrameAnimationDriver();

	void onScreenAdded(QScreen* screen);
	void updateInterval();

	// Fires only when no frame advanced the clock in time, so animations nothing renders keep moving.
	QChronoTimer fallback;
};

} // namespace qs::core
