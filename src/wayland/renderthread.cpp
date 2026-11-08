#include "renderthread.hpp"
#include <functional>
#include <memory>

#include <qcoreapplication.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qstring.h>
#include <qsurfaceformat.h>
#include <qtenvironmentvariables.h>
#include <qthread.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

namespace qs::wayland::render {

std::shared_ptr<RenderThread> RenderThread::acquire(const QSurfaceFormat& format) {
	static std::weak_ptr<RenderThread> shared;
	if (auto thread = shared.lock()) return thread;
	auto thread = std::make_shared<RenderThread>(format);
	shared = thread;
	return thread;
}

// QOffscreenSurface must be created on the GUI thread.
RenderThread::RenderThread(const QSurfaceFormat& format, QRhi::Implementation backend)
    : surface(
          backend == QRhi::OpenGLES2 ? QRhiGles2InitParams::newFallbackSurface(format) : nullptr
      )
    , threaded(qEnvironmentVariable("QS_RENDER_THREAD") != "0") {
	if (this->threaded) {
		this->renderThread.setObjectName(QStringLiteral("qs-render"));
		this->worker.moveToThread(&this->renderThread);
		this->renderThread.start();
	}
	this->invoke([this, format, backend] {
		if (backend == QRhi::Null) {
			QRhiNullInitParams params;
			this->device.reset(QRhi::create(QRhi::Null, &params));
			return;
		}

		// Keep the context offscreen; leave the window out of the init params.
		QRhiGles2InitParams params;
		params.format = format;
		params.fallbackSurface = this->surface.get();
		this->device.reset(QRhi::create(QRhi::OpenGLES2, &params));
	});
}

RenderThread::~RenderThread() {
	this->invoke([this] {
		this->device.reset();
		this->worker.moveToThread(QCoreApplication::instance()->thread());
	});
	if (!this->threaded) return;
	this->renderThread.quit();
	this->renderThread.wait();
}

void RenderThread::invoke(const std::function<void()>& work) {
	if (QThread::currentThread() == this->worker.thread()) {
		work();
		return;
	}
	QMetaObject::invokeMethod(&this->worker, work, Qt::BlockingQueuedConnection);
}

void RenderThread::submit(const std::function<void()>& work) {
	if (!this->threaded) {
		work();
		return;
	}
	QMetaObject::invokeMethod(&this->worker, work, Qt::QueuedConnection);
}

} // namespace qs::wayland::render
