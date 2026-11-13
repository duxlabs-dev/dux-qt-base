#include "renderthread.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include <private/qvulkandefaultinstance_p.h>
#include <qabstractanimation.h>
#include <qalgorithms.h>
#include <qcontainerfwd.h>
#include <qcoreapplication.h>
#include <qlist.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qquickgraphicsconfiguration.h>
#include <qquickwindow.h>
#include <qsgrendererinterface.h>
#include <qsize.h>
#include <qstring.h>
#include <qsurfaceformat.h>
#include <qtenvironmentvariables.h>
#include <qthread.h>
#include <qtimer.h>
#include <qtypes.h>
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

// Leaked so no static destructor ends a thread; acquire() empties it as the application quits.
QList<RenderThread*>& idleThreads() {
	static auto* threads = new QList<RenderThread*>(); // NOLINT
	return *threads;
}

constexpr qsizetype MAX_IDLE_THREADS = 1;
// Past the half second Mesa keeps a freed buffer; see pb_cache_init() callers.
constexpr auto SWEEP_DELAY = std::chrono::seconds(2);
constexpr int SWEEP_EDGE = 64;
// Windows destroyed while the application quits end their threads.
bool IDLE_CLOSED = false; // NOLINT

// newFallbackSurface() creates a context to resolve the format, so resolve each one once.
QOffscreenSurface* createFallbackSurface(const QSurfaceFormat& format) {
	static auto* resolved = new QList<std::pair<QSurfaceFormat, QSurfaceFormat>>(); // NOLINT
	const auto requested = [&format](const auto& entry) { return entry.first == format; };
	const auto found = std::ranges::find_if(*resolved, requested);

	if (found == resolved->end()) {
		auto* surface = QRhiGles2InitParams::newFallbackSurface(format);
		resolved->append({format, surface->format()});
		return surface;
	}

	auto* surface = new QOffscreenSurface();
	surface->setFormat(found->second);
	surface->create();
	return surface;
}

} // namespace

void RenderThread::prepareWindow(QQuickWindow* window) {
	if (QQuickWindow::graphicsApi() != QSGRendererInterface::Vulkan) return;
	// A window with a render control gets no instance; see QSGRhiSupport::prepareWindowForRhi().
	if (!window->vulkanInstance()) window->setVulkanInstance(QVulkanDefaultInstance::instance());
}

std::unique_ptr<RenderThread> RenderThread::acquire(QQuickWindow* window) {
	static auto connected = false;
	if (!std::exchange(connected, true)) {
		auto* app = QCoreApplication::instance();
		QObject::connect(app, &QCoreApplication::aboutToQuit, app, [] {
			IDLE_CLOSED = true;
			qDeleteAll(std::exchange(idleThreads(), {}));
		});
	}

	auto& idle = idleThreads();
	const auto format = window->requestedFormat();
	const auto suits = [&format](const RenderThread* thread) { return thread->format == format; };
	const auto found = std::ranges::find_if(idle, suits);
	if (found == idle.end()) return std::make_unique<RenderThread>(window);

	auto thread = std::unique_ptr<RenderThread>(*found);
	idle.erase(found);
	RenderThread::prepareWindow(window);
	return thread;
}

void RenderThread::recycle(std::unique_ptr<RenderThread> thread) {
	if (IDLE_CLOSED) return;

	auto& idle = idleThreads();
	if (idle.size() >= MAX_IDLE_THREADS) delete idle.takeFirst();
	thread->sweepLater();
	idle.append(thread.release());
}

void RenderThread::sweepLater() { this->sweepTimer.start(); }

// QRhiGles2 frees released GL objects in beginOffscreenFrame(), QRhiVulkan only those of
// one frame slot there and the rest in finish(); see executeDeferredReleases().
void RenderThread::freeReleased() {
	if (!this->device) return;

	if (this->device->backend() == QRhi::Vulkan) {
		this->device->finish();
		return;
	}

	QRhiCommandBuffer* commands = nullptr;
	if (this->device->beginOffscreenFrame(&commands) != QRhi::FrameOpSuccess) return;

	// GL keeps the attachments of the target drawn last until another target is drawn.
	if (this->parking) {
		commands->beginPass(this->parking.get(), Qt::transparent, {1.0F, 0});
		commands->endPass();
	}

	this->device->endOffscreenFrame();
}

// Mesa caches a freed buffer and drops it only when a later free finds it expired; see
// pb_cache_add_buffer().
void RenderThread::sweep() {
	if (!this->device) return;

	const auto size = QSize(SWEEP_EDGE, SWEEP_EDGE);
	auto texture = std::unique_ptr<QRhiTexture>(this->device->newTexture(QRhiTexture::RGBA8, size));
	if (!texture->create()) return;

	texture.reset();
	this->freeReleased();
}

RenderThread::RenderThread(QQuickWindow* window)
    : format(window->requestedFormat())
    , threaded(qEnvironmentVariable("QS_RENDER_THREAD") != "0") {
	const auto vulkan = QQuickWindow::graphicsApi() == QSGRendererInterface::Vulkan;
	RenderThread::prepareWindow(window);

	if (!vulkan) this->surface.reset(createFallbackSurface(this->format));
	auto* instance = window->vulkanInstance();
	const auto extensions = window->graphicsConfiguration().deviceExtensions();

	this->sweepTimer.setSingleShot(true);
	this->sweepTimer.setInterval(SWEEP_DELAY);
	QObject::connect(&this->sweepTimer, &QTimer::timeout, &this->sweepTimer, [this] {
		this->submit([this] { this->sweep(); });
	});

	if (this->threaded) {
		this->renderThread.setObjectName(QStringLiteral("qs-render"));
		this->worker.moveToThread(&this->renderThread);
		this->renderThread.start();
	}
	this->invoke([this, vulkan, instance, extensions] {
		if (vulkan) this->device.reset(createVulkanDevice(instance, extensions));
		else this->device.reset(createGlesDevice(this->format, this->surface.get()));
		if (!vulkan && this->device) this->createParking();

		// Qt's default driver steps this thread's animators on a 16 ms timer, not per frame.
		if (!this->threaded) return;
		this->animators = std::make_unique<QAnimationDriver>();
		this->animators->install();
	});
}

void RenderThread::createParking() {
	const auto flags = QRhiTexture::RenderTarget;
	this->parkingTexture.reset(this->device->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1, flags));
	if (!this->parkingTexture->create()) return;

	auto target = std::unique_ptr<QRhiTextureRenderTarget>(
	    this->device->newTextureRenderTarget({QRhiColorAttachment(this->parkingTexture.get())})
	);
	this->parkingPass.reset(target->newCompatibleRenderPassDescriptor());
	target->setRenderPassDescriptor(this->parkingPass.get());
	if (target->create()) this->parking = std::move(target);
}

RenderThread::~RenderThread() {
	this->invoke([this] {
		this->animators.reset();
		this->parking.reset();
		this->parkingPass.reset();
		this->parkingTexture.reset();
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
