#pragma once

#include <functional>
#include <memory>

#include <qobject.h>
#include <qoffscreensurface.h>
#include <qsurfaceformat.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <rhi/qrhi.h>

namespace qs::wayland::render {

class RenderThread {
public:
	static std::shared_ptr<RenderThread> acquire(const QSurfaceFormat& format);
	explicit RenderThread(
	    const QSurfaceFormat& format,
	    QRhi::Implementation backend = QRhi::OpenGLES2
	);
	~RenderThread();
	Q_DISABLE_COPY_MOVE(RenderThread);

	void invoke(const std::function<void()>& work);
	void submit(const std::function<void()>& work);
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
