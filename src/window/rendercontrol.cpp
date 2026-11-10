#include "rendercontrol.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpoint.h>
#include <qquickwindow.h>
#include <qsgrendererinterface.h>
#include <qwindow.h>

#include "../core/animationdriver.hpp"
#include "../core/logcat.hpp"

namespace qs::window {

namespace {
QS_LOGGING_CATEGORY(logRenderControl, "quickshell.window.rendercontrol", QtWarningMsg);
WindowRenderControl* (*FACTORY)() = nullptr; // NOLINT
bool (*USABLE)() = nullptr;                  // NOLINT
bool ENABLED = false;                        // NOLINT

bool supported() {
	if (!FACTORY || !USABLE) {
		qCInfo(logRenderControl) << "Render control is unavailable on this platform.";
		return false;
	}

	const auto api = QQuickWindow::graphicsApi();
	if (api != QSGRendererInterface::OpenGL && api != QSGRendererInterface::Vulkan) {
		qCInfo(logRenderControl) << "Render control requires OpenGL or Vulkan.";
		return false;
	}

	if (!USABLE()) {
		qCWarning(logRenderControl) << "Render control is unavailable: the renderer cannot draw "
		                               "into dmabufs. Windows use Qt's render loop.";
		return false;
	}

	return true;
}
} // namespace

// Windows without a render control stay on Qt's render loop, which needs its own animation driver.
void WindowRenderControl::setEnabled(bool enabled) {
	ENABLED = enabled && supported();
	if (ENABLED) qs::core::FrameAnimationDriver::install();
}

void WindowRenderControl::setFactory(WindowRenderControl* (*factory)(), bool (*usable)()) {
	FACTORY = factory;
	USABLE = usable;
}

WindowRenderControl* WindowRenderControl::create() {
	if (!ENABLED) return nullptr;
	return FACTORY();
}

// Use the window's scale; see QQuickWindow::effectiveDevicePixelRatio().
QWindow* WindowRenderControl::renderWindow(QPoint* offset) {
	if (offset) *offset = QPoint();
	return this->window();
}

} // namespace qs::window
