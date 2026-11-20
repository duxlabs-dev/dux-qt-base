#pragma once

// Temporary instrumentation for the render-thread UAF investigation (QS_RACE_TRACE=1).
// Logs thread identity at the GUI/render-thread crossings. Remove before merging.

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
