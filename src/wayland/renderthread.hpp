#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <qobject.h>
#include <qoffscreensurface.h>
#include <qsurfaceformat.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <rhi/qrhi.h>

namespace qs::wayland::render {

class RenderThread {
public:
	explicit RenderThread(const QSurfaceFormat& format);
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
	QThread* thread() { return this->worker.thread(); }

private:
	QThread renderThread;
	QObject worker;
	std::unique_ptr<QOffscreenSurface> surface;
	std::unique_ptr<QRhi> device;
	bool threaded;
};

} // namespace qs::wayland::render
