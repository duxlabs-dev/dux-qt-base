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
bool ENABLED = false;                        // NOLINT

bool supported() {
	if (!FACTORY) {
		qCInfo(logRenderControl) << "Render control is unavailable on this platform.";
		return false;
	}

	if (QQuickWindow::graphicsApi() != QSGRendererInterface::OpenGL) {
		qCInfo(logRenderControl) << "Render control requires OpenGL.";
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

void WindowRenderControl::setFactory(WindowRenderControl* (*factory)()) { FACTORY = factory; }

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
