#include "renderthread.hpp"
#include <functional>
#include <memory>

#include <qabstractanimation.h>
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

RenderThread::RenderThread(const QSurfaceFormat& format)
    : surface(QRhiGles2InitParams::newFallbackSurface(format))
    , threaded(qEnvironmentVariable("QS_RENDER_THREAD") != "0") {
	if (this->threaded) {
		this->renderThread.setObjectName(QStringLiteral("qs-render"));
		this->worker.moveToThread(&this->renderThread);
		this->renderThread.start();
	}
	this->invoke([this, format] {
		QRhiGles2InitParams params;
		params.format = format;
		params.fallbackSurface = this->surface.get();
		this->device.reset(QRhi::create(QRhi::OpenGLES2, &params));

		// Qt's default driver steps this thread's animators on a 16 ms timer, not per frame.
		if (!this->threaded) return;
		this->animators = std::make_unique<QAnimationDriver>();
		this->animators->install();
	});
}

RenderThread::~RenderThread() {
	this->invoke([this] {
		this->animators.reset();
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

} // namespace qs::wayland::render
