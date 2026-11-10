#include "renderthread.hpp"
#include <functional>
#include <memory>

#include <private/qvulkandefaultinstance_p.h>
#include <qabstractanimation.h>
#include <qcontainerfwd.h>
#include <qcoreapplication.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qquickgraphicsconfiguration.h>
#include <qquickwindow.h>
#include <qsgrendererinterface.h>
#include <qstring.h>
#include <qsurfaceformat.h>
#include <qtenvironmentvariables.h>
#include <qthread.h>
#include <qvulkaninstance.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

namespace qs::wayland::render {

namespace {

QRhi* createGlesDevice(const QSurfaceFormat& format, QOffscreenSurface* surface) {
	QRhiGles2InitParams params;
	params.format = format;
	params.fallbackSurface = surface;
	return QRhi::create(QRhi::OpenGLES2, &params);
}

QRhi* createVulkanDevice(QVulkanInstance* instance, const QByteArrayList& extensions) {
	if (!instance) return nullptr;

	QRhiVulkanInitParams params;
	params.inst = instance;
	params.deviceExtensions = extensions;
	return QRhi::create(QRhi::Vulkan, &params);
}

} // namespace

RenderThread::RenderThread(QQuickWindow* window)
    : threaded(qEnvironmentVariable("QS_RENDER_THREAD") != "0") {
	const auto format = window->requestedFormat();
	const auto vulkan = QQuickWindow::graphicsApi() == QSGRendererInterface::Vulkan;

	// A window with a render control gets no instance; see QSGRhiSupport::prepareWindowForRhi().
	if (vulkan && !window->vulkanInstance()) {
		window->setVulkanInstance(QVulkanDefaultInstance::instance());
	}

	if (!vulkan) this->surface.reset(QRhiGles2InitParams::newFallbackSurface(format));
	auto* instance = window->vulkanInstance();
	const auto extensions = window->graphicsConfiguration().deviceExtensions();

	if (this->threaded) {
		this->renderThread.setObjectName(QStringLiteral("qs-render"));
		this->worker.moveToThread(&this->renderThread);
		this->renderThread.start();
	}
	this->invoke([this, format, vulkan, instance, extensions] {
		if (vulkan) this->device.reset(createVulkanDevice(instance, extensions));
		else this->device.reset(createGlesDevice(format, this->surface.get()));

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
