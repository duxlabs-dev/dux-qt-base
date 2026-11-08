#pragma once

#include <cstdint>

#include <qset.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qwayland-presentation-time.h>
#include <qwaylandclientextension.h>
#include <wayland-client-protocol.h>

namespace qs::wayland::render {

class PresentationFeedback;

class PresentationTracker
    : public QWaylandClientExtensionTemplate<PresentationTracker>
    , public QtWayland::wp_presentation {
	Q_OBJECT;

public:
	PresentationTracker();
	~PresentationTracker() override;
	Q_DISABLE_COPY_MOVE(PresentationTracker);

	PresentationFeedback* track(wl_surface* surface, quint64 frame, qint64 requested, qint64 input);
	static void cancel(PresentationFeedback* feedback);
	static qint64 now();

protected:
	void wp_presentation_clock_id(uint32_t clockId) override;

private:
	friend class PresentationFeedback;
	QSet<PresentationFeedback*> pending;
	bool monotonic = false;
};

} // namespace qs::wayland::render
