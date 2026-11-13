#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <qabstractanimation.h>
#include <qobject.h>
#include <qoffscreensurface.h>
#include <qquickwindow.h>
#include <qsurfaceformat.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtimer.h>
#include <rhi/qrhi.h>

namespace qs::wayland::render {

class RenderThread {
public:
	explicit RenderThread(QQuickWindow* window);
	~RenderThread();
	Q_DISABLE_COPY_MOVE(RenderThread);

	// An idle thread whose device suits `window`, or a new one.
	static std::unique_ptr<RenderThread> acquire(QQuickWindow* window);
	// Keeps `thread` for the next window. Its device must hold no scene.
	static void recycle(std::unique_ptr<RenderThread> thread);
	// Render thread only. Frees what the device's owners released.
	void freeReleased();
	// Has the driver drop the buffers it cached from those frees, once they expired there.
	void sweepLater();

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
	static void prepareWindow(QQuickWindow* window);
	// Render thread only.
	void createParking();
	// Render thread only.
	void sweep();

	QSurfaceFormat format;
	QThread renderThread;
	QObject worker;
	std::unique_ptr<QOffscreenSurface> surface;
	std::unique_ptr<QRhi> device;
	std::unique_ptr<QRhiTexture> parkingTexture;
	std::unique_ptr<QRhiRenderPassDescriptor> parkingPass;
	std::unique_ptr<QRhiTextureRenderTarget> parking;
	std::unique_ptr<QAnimationDriver> animators;
	QTimer sweepTimer;
	bool threaded;
};

} // namespace qs::wayland::render
