#pragma once

// QS_RACE_TRACE=1 logs thread identity at the GUI/render-thread crossings.
// Kept for field post-mortems of the render-thread UAF family; zero-cost when disabled.

#include <qlogging.h>
#include <qthread.h>

namespace qs::wayland::render {

inline bool raceTrace() {
	static const bool enabled = qEnvironmentVariableIsSet("QS_RACE_TRACE");
	return enabled;
}

} // namespace qs::wayland::render

#define RT_TRACE(...)                                                                              \
	do {                                                                                           \
		if (qs::wayland::render::raceTrace()) [[unlikely]] {                                       \
			auto* t = QThread::currentThread();                                                    \
			qWarning() << "[rt]" << QThread::currentThreadId()                                     \
			           << (t ? t->objectName() : QStringLiteral("?")) << __FUNCTION__ << __VA_ARGS__; \
		}                                                                                          \
	} while (false)
