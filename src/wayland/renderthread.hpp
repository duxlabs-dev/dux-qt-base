#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <qabstractanimation.h>
#include <qobject.h>
#include <qoffscreensurface.h>
#include <qquickwindow.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <rhi/qrhi.h>

namespace qs::wayland::render {

class RenderThread {
public:
	explicit RenderThread(QQuickWindow* window);
	~RenderThread();
	Q_DISABLE_COPY_MOVE(RenderThread);

	void invoke(const std::function<void()>& work);
	template <typename F>
	void submit(F&& work) {
		if (!this->threaded) {
			work();
			return;
		}
		QMetaObject::invokeMethod(&this->worker, std::forward<F>(work), Qt::QueuedConnection);
	}
	[[nodiscard]] QRhi* rhi() const { return this->device.get(); }
	// Null when rendering on the GUI thread, whose animators follow its own driver.
	[[nodiscard]] QAnimationDriver* animatorDriver() const { return this->animators.get(); }
	QThread* thread() { return this->worker.thread(); }

private:
	QThread renderThread;
	QObject worker;
	std::unique_ptr<QOffscreenSurface> surface;
	std::unique_ptr<QRhi> device;
	std::unique_ptr<QAnimationDriver> animators;
	bool threaded;
};

} // namespace qs::wayland::render
