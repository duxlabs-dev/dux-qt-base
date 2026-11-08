#include "presentation.hpp"
#include <cstdint>
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <time.h>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qtclasshelpermacros.h>
#include <qtypes.h>
#include <qwayland-presentation-time.h>
#include <qwaylandclientextension.h>
#include <wayland-client-protocol.h>
#include <wayland-presentation-time-client-protocol.h>

#include "../core/logcat.hpp"

namespace qs::wayland::render {

namespace {
QS_LOGGING_CATEGORY(logPresentation, "quickshell.wayland.rendercontrol.presentation", QtWarningMsg);
}

class PresentationFeedback: public QtWayland::wp_presentation_feedback {
public:
	PresentationFeedback(
	    PresentationTracker* owner,
	    struct ::wp_presentation_feedback* feedback,
	    quint64 frame,
	    qint64 requested,
	    qint64 input
	)
	    : QtWayland::wp_presentation_feedback(feedback)
	    , owner(owner)
	    , frame(frame)
	    , requested(requested)
	    , input(input) {}

	~PresentationFeedback() override {
		this->owner->pending.remove(this);
		wp_presentation_feedback_destroy(this->object());
	}
	Q_DISABLE_COPY_MOVE(PresentationFeedback);

protected:
	void wp_presentation_feedback_presented(
	    uint32_t secondsHi,
	    uint32_t secondsLo,
	    uint32_t nanoseconds,
	    uint32_t refresh,
	    uint32_t /*seqHi*/,
	    uint32_t /*seqLo*/,
	    uint32_t flags
	) override {
		const auto seconds = (static_cast<quint64>(secondsHi) << 32U) | secondsLo;
		const auto presented = static_cast<qint64>(seconds * 1000000000 + nanoseconds);
		const auto elapsedUs = [&](qint64 since) {
			return this->owner->monotonic && since ? (presented - since) / 1000 : -1;
		};

		qCDebug(logPresentation) << "frame" << this->frame << "presented_ns" << presented
		                         << "refresh_ns" << refresh << "request_to_present_us"
		                         << elapsedUs(this->requested) << "input_to_present_us"
		                         << elapsedUs(this->input) << "flags" << flags;
		delete this;
	}

	void wp_presentation_feedback_discarded() override {
		qCDebug(logPresentation) << "frame" << this->frame << "discarded";
		delete this;
	}

private:
	PresentationTracker* owner;
	quint64 frame;
	qint64 requested;
	qint64 input;
};

PresentationTracker::PresentationTracker(): QWaylandClientExtensionTemplate(1) {
	this->initialize();
}

PresentationTracker::~PresentationTracker() {
	while (!this->pending.isEmpty()) delete *this->pending.begin();
	if (this->isActive()) this->destroy();
}

qint64 PresentationTracker::now() {
	timespec time {};
	clock_gettime(CLOCK_MONOTONIC, &time); // NOLINT(misc-include-cleaner)
	return static_cast<qint64>(time.tv_sec) * 1000000000 + time.tv_nsec;
}

void PresentationTracker::wp_presentation_clock_id(uint32_t clockId) {
	this->monotonic = clockId == CLOCK_MONOTONIC;
}

PresentationFeedback*
PresentationTracker::track(wl_surface* surface, quint64 frame, qint64 requested, qint64 input) {
	if (!this->isActive() || !surface || this->pending.size() >= 64) return nullptr;
	auto* feedback = new PresentationFeedback(this, this->feedback(surface), frame, requested, input);
	this->pending.insert(feedback);
	return feedback;
}

void PresentationTracker::cancel(PresentationFeedback* feedback) { delete feedback; }

} // namespace qs::wayland::render
