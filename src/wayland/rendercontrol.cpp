#include "rendercontrol.hpp"
#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

#include <libdrm/drm_fourcc.h>
#include <private/qquickrendercontrol_p.h>
#include <private/qquickwindow_p.h>
#include <private/qsgrenderer_p.h>
#include <private/qwaylandabstractdecoration_p.h>
#include <private/qwaylandbuffer_p.h>
#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandwindow_p.h>
#include <qcoreapplication.h>
#include <qcoreevent.h>
#include <qelapsedtimer.h>
#include <qimage.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmargins.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qoffscreensurface.h> // NOLINT(misc-include-cleaner)
#include <qpa/qplatformwindow.h>
#include <qpainter.h>
#include <qpoint.h>
#include <qpointer.h>
#include <qquickgraphicsconfiguration.h>
#include <qquickgraphicsdevice.h>
#include <qquickrendertarget.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qregion.h>
#include <qscopeguard.h>
#include <qscreen.h>
#include <qsemaphore.h>
#include <qsgflatcolormaterial.h>
#include <qsggeometry.h>
#include <qsgmaterial.h>
#include <qsgnode.h>
#include <qsize.h>
#include <qsocketnotifier.h>
#include <qstring.h>
#include <qsurfaceformat.h>
#include <qtclasshelpermacros.h>
#include <qtenvironmentvariables.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <rhi/qrhi.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "../core/animationdriver.hpp"
#include "../core/logcat.hpp"
#include "../window/damage.hpp"
#include "../window/proxywindow.hpp"
#include "../window/rendercontrol.hpp"
#include "buffer/manager.hpp"
#include "buffer/shm.hpp"
#include "presentation.hpp"
#include "renderthread.hpp"

namespace qs::wayland::render {

namespace {

QS_LOGGING_CATEGORY(logRenderControl, "quickshell.wayland.rendercontrol", QtWarningMsg);

constexpr qsizetype MAX_BUFFERS = 3;
// Longer than a clock's tick, so its window keeps the buffers it repaints into.
constexpr auto TRIM_DELAY = std::chrono::seconds(3);
// Refreshes held frames wait for another window's callback.
constexpr auto FRAME_SYNC_REFRESHES = 1.5;
// Refreshes after which a callback is throttled, as for a hidden surface, and not waited for.
constexpr auto FRAME_SYNC_STALE_REFRESHES = 3;
quint64 FRAME_SERIAL = 0; // NOLINT

QS_LOGGING_CATEGORY(logFrame, "quickshell.wayland.rendercontrol.frame", QtWarningMsg);

// Detailed logs format every quickshell debug message regardless of its category level.
bool frameTracingEnabled() {
	static const bool enabled = qEnvironmentVariableIsSet("QS_FRAME_TRACE");
	return enabled;
}

// Borrow the wl_buffer for one commit; SurfaceBufferPool owns it and handles releases.
class WlBufferRef: public QtWaylandClient::QWaylandBuffer {
public:
	WlBufferRef(wl_buffer* buffer, QSize size): mSize(size) { this->mBuffer = buffer; }
	~WlBufferRef() override { this->mBuffer = nullptr; }
	Q_DISABLE_COPY_MOVE(WlBufferRef);

	[[nodiscard]] QSize size() const override { return this->mSize; }

private:
	QSize mSize;
};

// The readback keeps the texture's row order, which drawFrame() already mirrored to match the surface.
bool copyReadback(const QRhiReadbackResult& result, QImage* image) {
	const auto size = result.pixelSize;
	const auto length = static_cast<qsizetype>(size.width()) * size.height() * 4;
	if (result.format != QRhiTexture::RGBA8 || size != image->size() || result.data.size() < length) {
		return false;
	}

	const auto source = QImage(
	    reinterpret_cast<const uchar*>(result.data.constData()),
	    size.width(),
	    size.height(),
	    QImage::Format_RGBA8888_Premultiplied
	);

	auto painter = QPainter(image);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.drawImage(0, 0, source);
	return true;
}

// The whole units of `size` a region touches once scaled.
QRegion scaledRegion(const QRegion& unscaled, qreal scale, QSize size) {
	QRegion region;
	for (const auto& rect: unscaled) {
		const auto scaled = QRectF(rect.topLeft().toPointF() * scale, rect.size().toSizeF() * scale);
		region += scaled.toAlignedRect();
	}

	return region & QRect(QPoint(), size);
}

// Buffer pixels as the logical window damage that covers them.
QRegion windowRegion(const QRegion& pixels, qreal scale) {
	QRegion region;
	for (const auto& rect: pixels) {
		const auto scaled = QRectF(rect.topLeft().toPointF() / scale, rect.size().toSizeF() / scale);
		region += scaled.toAlignedRect();
	}

	return region;
}

qs::window::WindowRenderControl* createRenderControl() { return new WaylandRenderControl(); }

bool usable() { return buffer::WlBufferManager::instance()->initWithoutWindow(); }

void install() {
	if (qEnvironmentVariableIsSet("QS_DISABLE_DMABUF")) {
		qCInfo(logRenderControl) << "Render control is unavailable with QS_DISABLE_DMABUF.";
		return;
	}

	qs::window::WindowRenderControl::setFactory(&createRenderControl, &usable);
}

} // namespace

// Render thread only. Not owned by the scene graph, which detaches them when it is destroyed.
struct SceneClip {
	SceneClip() {
		// Nodes are owned by their parent by default, and these are members.
		this->fillClip.setFlag(QSGNode::OwnedByParent, false);
		this->fill.setFlag(QSGNode::OwnedByParent, false);
		this->scene.setFlag(QSGNode::OwnedByParent, false);
		this->fill.setGeometry(&this->geometry);
		this->fill.setMaterial(&this->material);
		// QSG_VISUALIZE=clip reads the geometry of every clip node; see RhiVisualizer::ClipVis::gather().
		this->fillClip.setGeometry(&this->geometry);
		this->scene.setGeometry(&this->geometry);
		this->fillClip.setIsRectangular(true);
		this->fillClip.appendChildNode(&this->fill);
		this->scene.setIsRectangular(true);
	}

	QSGGeometry geometry = QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
	QSGFlatColorMaterial material;
	QSizeF size;
	QSGClipNode fillClip;
	QSGGeometryNode fill;
	QSGClipNode scene;
};

SurfaceBuffer::~SurfaceBuffer() { this->pool->control->releaseBuffer(this); }

const wl_buffer_listener SurfaceBufferPool::LISTENER = {.release = &SurfaceBufferPool::onRelease};

void SurfaceBufferPool::onRelease(void* data, wl_buffer* /*buffer*/) {
	auto* buffer = static_cast<SurfaceBuffer*>(data);
	buffer->busy = false;
	buffer->pool->control->onBufferReleased();
}

SurfaceBuffer* SurfaceBufferPool::acquire(QSize size, bool alpha) {
	auto matches = [&](const std::unique_ptr<SurfaceBuffer>& buffer) {
		return buffer->size == size && buffer->alpha == alpha;
	};

	// Keep old buffers until onRelease() clears busy.
	std::erase_if(this->buffers, [&](const auto& buffer) {
		return !matches(buffer) && !buffer->busy;
	});

	for (const auto& buffer: this->buffers) {
		if (!matches(buffer) || buffer->busy) continue;
		this->current = buffer.get();
		return this->current;
	}

	this->current = this->create(size, alpha);
	return this->current;
}

void SurfaceBufferPool::clear() {
	this->current = nullptr;
	this->buffers.clear();
	this->control->releaseDepthStencil(this->depthStencil);
}

bool SurfaceBufferPool::trim() {
	const auto idle = [this](const auto& buffer) {
		return buffer.get() != this->current && !buffer->busy;
	};
	auto released = std::erase_if(this->buffers, idle) != 0;

	// prepare() builds these again; the pixels and their stale region stay with the buffer.
	for (const auto& buffer: this->buffers) {
		if (!buffer->renderTarget) continue;
		this->control->releaseBuffer(buffer.get());
		released = true;
	}

	if (this->depthStencil) {
		this->control->releaseDepthStencil(this->depthStencil);
		released = true;
	}

	return released;
}

bool SurfaceBufferPool::available(QSize size, bool alpha) const {
	auto used = 0;
	for (const auto& buffer: this->buffers) {
		if (!buffer->busy && buffer->size == size && buffer->alpha == alpha) return true;
		if (buffer->busy) used++;
	}

	return used < MAX_BUFFERS;
}

SurfaceBuffer* SurfaceBufferPool::create(QSize size, bool alpha) {
	buffer::WlBufferRequest request;
	request.width = size.width();
	request.height = size.height();
	request.dmabuf.renderTarget = true;
	request.dmabuf.formats.push(
	    buffer::WlBufferRequest::DmaFormat(alpha ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_XRGB8888)
	);

	auto result = std::make_unique<SurfaceBuffer>();
	result->pool = this;
	result->size = size;
	result->alpha = alpha;
	result->buffer.reset(buffer::WlBufferManager::instance()->createBuffer(request));

	if (!result->buffer) {
		static auto logged = false;
		if (!std::exchange(logged, true)) {
			qCWarning(logRenderControl) << "No dmabuf render target is available, frames are copied "
			                               "into shm buffers instead.";
		}

		const auto format = alpha ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888;
		result->shm = buffer::shm::WlShmTarget::create(size, format);
		result->buffer.reset(result->shm);
	}

	if (!result->buffer) return nullptr;
	wl_buffer_add_listener(result->buffer->buffer(), &LISTENER, result.get());

	if (frameTracingEnabled()) qCDebug(logFrame) << "allocate" << size;
	return this->buffers.emplace_back(std::move(result)).get();
}

bool SurfaceBufferPool::prepare(
    QQuickWindow* window,
    QRhi* rhi,
    SurfaceBuffer* buffer,
    bool partial
) {
	if (buffer->renderTarget) return true;
	const auto size = buffer->size;

	if (buffer->shm) {
		const auto flags = QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource;
		buffer->texture.reset(rhi->newTexture(QRhiTexture::RGBA8, size, 1, flags));
		if (!buffer->texture->create()) return false;
	} else {
		buffer->import.reset(buffer->buffer->createQsgTexture(window));
		if (!buffer->import) return false;

		// The image's own format: QRhiVulkan creates the view with it.
		auto* imported = buffer->import->texture()->rhiTexture();
		const auto native = imported->nativeTexture();
		buffer->texture.reset(rhi->newTexture(imported->format(), size, 1, QRhiTexture::RenderTarget));
		if (!buffer->texture->createFrom({.object = native.object, .layout = 0})) return false;
	}

	if (!this->depthStencil || this->depthStencil->pixelSize() != size) {
		this->depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size));
		if (!this->depthStencil->create()) {
			this->depthStencil.reset();
			return false;
		}
	}

	buffer->depthStencil = this->depthStencil;

	auto description = QRhiTextureRenderTargetDescription(QRhiColorAttachment(buffer->texture.get()));
	description.setDepthStencilBuffer(buffer->depthStencil.get());
	buffer->renderTarget.reset(rhi->newTextureRenderTarget(description));
	buffer->renderPass.reset(buffer->renderTarget->newCompatibleRenderPassDescriptor());
	buffer->renderTarget->setRenderPassDescriptor(buffer->renderPass.get());
	if (!buffer->renderTarget->create()) {
		buffer->renderTarget.reset();
		return false;
	}

	if (!partial || buffer->shm) return true;
	const auto preserve = QRhiTextureRenderTarget::PreserveColorContents;
	buffer->partialTarget.reset(rhi->newTextureRenderTarget(description, preserve));
	buffer->partialPass.reset(buffer->partialTarget->newCompatibleRenderPassDescriptor());
	buffer->partialTarget->setRenderPassDescriptor(buffer->partialPass.get());
	if (!buffer->partialTarget->create()) buffer->partialTarget.reset();
	return true;
}

void SurfaceBufferPool::drawn(SurfaceBuffer* buffer, const std::optional<QRegion>& changed) {
	for (const auto& other: this->buffers) {
		if (other.get() == buffer) other->stale = QRegion();
		else if (!changed) other->stale.reset();
		else if (other->stale) *other->stale += *changed;
	}
}

WaylandRenderControl::WaylandRenderControl(): pool(this) {
	this->trimTimer.setSingleShot(true);
	this->trimTimer.setInterval(TRIM_DELAY);

	// clang-format off
	QObject::connect(this, &QQuickRenderControl::renderRequested, this, &WaylandRenderControl::onRenderRequested);
	QObject::connect(this, &QQuickRenderControl::sceneChanged, this, &WaylandRenderControl::scheduleFrame);
	QObject::connect(&this->trimTimer, &QTimer::timeout, this, &WaylandRenderControl::trimBuffers);
	QObject::connect(&this->fenceNotifier, &QSocketNotifier::activated, this, &WaylandRenderControl::onFenceSignaled);
	// clang-format on

	frameSync().controls.append(this);
}

WaylandRenderControl::Frame::~Frame() {
	if (this->fence != -1) close(this->fence);
	if (!this->drawing) return;

	// Dropped with its window, possibly on the render thread; held frames must not wait for it.
	auto* app = QCoreApplication::instance();
	if (!app) return;
	QMetaObject::invokeMethod(
	    app,
	    [screen = this->screen] {
		    --frameSync().screens.tryEmplace(screen).iterator->drawing;
		    commitHeldFrames();
	    },
	    Qt::QueuedConnection
	);
}

WaylandRenderControl::FrameSync& WaylandRenderControl::frameSync() {
	static FrameSync sync;
	return sync;
}

void WaylandRenderControl::commitHeldFrames() {
	auto& sync = frameSync();
	const auto now = PresentationTracker::now();
	qint64 wait = 0;
	HeldFrames ready;

	for (auto it = sync.screens.begin(); it != sync.screens.end();) {
		auto& held = it.value();
		if (held.frames.isEmpty()) {
			it = held.drawing > 0 ? std::next(it) : sync.screens.erase(it);
			continue;
		}

		// Frames on this screen still drawing or about to draw land a refresh later at most; waiting
		// for them puts all on one repaint, and their callbacks stay in step from then on.
		auto refresh = 1e9 / 60.0;
		auto waiting = held.drawing > 0;
		for (auto* control: std::as_const(sync.controls)) {
			auto* window = control->window();
			auto* screen = window ? window->screen() : nullptr;
			if (!screen || screen != it.key()) continue;

			if (screen->refreshRate() > 0) refresh = 1e9 / screen->refreshRate();
			const auto since = control->frameQueued ? now : control->awaitingSince;
			if (since && static_cast<double>(now - since) <= FRAME_SYNC_STALE_REFRESHES * refresh) {
				waiting = true;
			}
		}

		const auto deadline = held.heldSince + static_cast<qint64>(FRAME_SYNC_REFRESHES * refresh);
		if (waiting && deadline > now) {
			wait = wait > 0 ? std::min(wait, deadline - now) : deadline - now;
			++it;
			continue;
		}
		ready.append(std::exchange(held.frames, {}));
		it = held.drawing > 0 ? std::next(it) : sync.screens.erase(it);
	}

	if (wait > 0) {
		if (!sync.timer) {
			sync.timer = new QTimer(QCoreApplication::instance());
			sync.timer->setSingleShot(true);
			sync.timer->setTimerType(Qt::PreciseTimer);
			QObject::connect(
			    sync.timer,
			    &QTimer::timeout,
			    sync.timer,
			    &WaylandRenderControl::commitHeldFrames
			);
		}
		sync.timer->start(std::chrono::ceil<std::chrono::milliseconds>(std::chrono::nanoseconds(wait)));
	} else if (sync.timer) {
		sync.timer->stop();
	}

	// Controls destroyed while held are skipped, and their frames dropped.
	for (const auto& [control, frame]: std::as_const(ready)) {
		if (control) control->completeFrame(frame);
	}
}

void WaylandRenderControl::queueCommitHeldFrames() {
	auto* app = QCoreApplication::instance();
	if (!app) return;
	QMetaObject::invokeMethod(app, [] { commitHeldFrames(); }, Qt::QueuedConnection);
}

WaylandRenderControl::~WaylandRenderControl() {
	// The fence closes with pendingFrame, after the notifier would still watch it.
	this->fenceNotifier.setEnabled(false);
	frameSync().controls.removeOne(this);
	// Held frames that waited for this window commit once it is gone.
	if (this->awaitingSince || this->frameQueued) queueCommitHeldFrames();
	this->releaseScene();
}

// initializeScene() builds the scene and its thread again.
void WaylandRenderControl::releaseScene() {
	if (!this->renderThread || !this->rhi) return;

	this->renderThread->invoke([this] {
		this->rhi->makeThreadLocalNativeContextCurrent();
		// Invalidate before releasing rhi; QQuickRenderControl::~QQuickRenderControl() is too late.
		this->invalidate();
		// The scene graph is gone, so these nodes are detached; see QSGNode::destroy().
		this->sceneClip.reset();
		this->decoration.reset();
		this->prepareThread(this->thread());
		this->renderThread->detach(this->window());
	});
	this->pool.clear();
	const auto lost = this->renderThread->deviceLost();
	this->rhi = nullptr;

	if (lost) {
		this->renderThread.reset();
		return;
	}

	this->renderThread->invoke([this] { this->renderThread->freeReleased(); });
	RenderThread::recycle(std::move(this->renderThread));
}

void WaylandRenderControl::releaseBuffer(SurfaceBuffer* buffer) {
	if (!this->renderThread) return;
	this->renderThread->invoke([this, buffer] {
		// WlDmaBufferQSGTexture's destructor needs the current context.
		if (this->rhi) this->rhi->makeThreadLocalNativeContextCurrent();
		buffer->renderTarget.reset();
		buffer->partialTarget.reset();
		buffer->partialPass.reset();
		buffer->renderPass.reset();
		buffer->depthStencil.reset();
		buffer->texture.reset();
		buffer->import.reset();
	});
}

void WaylandRenderControl::releaseDepthStencil(std::shared_ptr<QRhiRenderBuffer>& buffer) {
	if (!this->renderThread) return;
	this->renderThread->invoke([&buffer] { buffer.reset(); });
}

void WaylandRenderControl::requestCommit() {
	this->commitRequested = true;
	++this->commitRevision;
	this->scheduleFrame();
}

// renderFrame() syncs animation and polish changes in the current frame.
void WaylandRenderControl::scheduleFrame() {
	if (this->presentation && !this->requestedAt) this->requestedAt = PresentationTracker::now();
	if (this->polishing || this->frameQueued) return;
	if (this->frameInFlight) {
		this->frameRequested = true;
		return;
	}
	auto* window = this->window();
	// An unexposed window starts no frame, so its request would only speed up the fallback.
	if (window->isExposed()) {
		qs::core::FrameAnimationDriver::requestFrame(window);
		if (!this->awaitingSince) this->awaitingSince = PresentationTracker::now();
	}
	window->requestUpdate();
}

// Explicit repaint requests bypass the damage check in renderFrame().
void WaylandRenderControl::onRenderRequested() {
	this->repaintRequested = true;
	this->scheduleFrame();
}

void WaylandRenderControl::onBufferReleased() {
	this->trimTimer.start();
	if (std::exchange(this->bufferAwaited, false)) this->scheduleFrame();
}

void WaylandRenderControl::trimBuffers() {
	// Hidden before the first frame.
	if (!this->rhi) return;
	if (this->frameInFlight) {
		this->trimTimer.start();
		return;
	}
	// renderFrame() builds the scene again once the window draws.
	if (this->pixelAttached) {
		this->releaseScene();
		return;
	}
	if (!this->pool.trim()) return;

	this->renderThread->invoke([this] { this->renderThread->freeReleased(); });
	this->renderThread->sweepLater();
}

bool WaylandRenderControl::eventFilter(QObject* watched, QEvent* event) {
	if (watched != this->window()) return false;

	switch (event->type()) {
	case QEvent::KeyPress:
	case QEvent::MouseButtonPress:
	case QEvent::MouseMove:
	case QEvent::Wheel:
	case QEvent::TouchBegin:
		if (this->presentation) this->inputAt = PresentationTracker::now();
		break;
	case QEvent::UpdateRequest:
		this->awaitingSince = 0;
		this->queueFrame();
		break;
	case QEvent::Expose:
		if (!this->window()->isExposed()) {
			this->presented = false;
			// Queued, since another window may be polishing and about to draw.
			if (std::exchange(this->awaitingSince, 0)) queueCommitHeldFrames();
			qs::core::FrameAnimationDriver::cancelFrame(this->window());
			// The compositor releases the attached buffer after the unmap; trim it with the rest.
			this->pool.forget();
			this->trimTimer.start();
			break;
		}
		// Commit surface state even if renderFrame() finds no damage.
		this->commitRequested = true;
		++this->commitRevision;
		// Deliver any update left pending while the window was unexposed.
		if (this->window()->handle()->hasPendingUpdateRequest()) {
			this->window()->handle()->deliverUpdateRequest();
			break;
		}
		this->queueFrame();
		break;
	default: break;
	}

	return false;
}

bool WaylandRenderControl::initializeScene() {
	auto* window = this->window();

	// Connect the tracker before QQuickRenderControl::initialize() emits sceneGraphInitialized.
	auto* base = qobject_cast<QsQuickWindowBase*>(window);
	if (!this->tracker && base) this->tracker = new qs::window::WindowDamageTracker(base);

	if (!this->presentation && frameTracingEnabled()) {
		this->presentation = std::make_unique<PresentationTracker>();
	}

	auto* manager = buffer::WlBufferManager::instance();
	this->renderThread = RenderThread::acquire(window);
	this->rhi = this->renderThread->rhi();
	auto initialized = false;
	if (this->rhi) {
		window->setGraphicsDevice(QQuickGraphicsDevice::fromRhi(this->rhi));
		this->prepareThread(this->renderThread->thread());
		this->renderThread->invoke([this, window, manager, &initialized] {
			// Before Qt 6.11 initRhi() creates a missing offscreen surface on this thread, a hidden
			// window whose QWaylandSurface then takes screenRemoved here, after the screen is freed.
			auto* control = QQuickRenderControlPrivate::get(this);
			control->offscreenSurface = this->renderThread->fallbackSurface();
			initialized = this->initialize();
			// resetRhi() would delete it.
			control->offscreenSurface = nullptr;
			if (initialized) {
				manager->initWindow(window);
				this->renderThread->attach(window);
				return;
			}
			this->invalidate();
			this->prepareThread(this->thread());
		});
	}

	if (!initialized) {
		this->rhi = nullptr;
		this->renderThread.reset();
		if (this->recoveringDevice) {
			qCWarning(logRenderControl) << "Graphics device still lost for" << window;
			return false;
		}
		emit window->sceneGraphError(
		    QQuickWindow::ContextNotAvailable,
		    QStringLiteral("Failed to initialize the render control.")
		);
		return false;
	}
	this->recoveringDevice = false;

	if (!manager->isReady()) {
		// clang-format off
		QObject::connect(manager, &buffer::WlBufferManager::ready, this, &WaylandRenderControl::scheduleFrame, Qt::SingleShotConnection);
		// clang-format on
	}

	return true;
}

void WaylandRenderControl::queueFrame() {
	if (!this->window()->isExposed() || this->frameQueued) return;
	if (this->frameInFlight) {
		this->frameRequested = true;
		return;
	}
	this->frameQueued = true;
	static QList<QPointer<WaylandRenderControl>> pending;
	pending.append(this);
	if (pending.size() != 1) return;

	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [] {
		    const auto frames = std::exchange(pending, {});
		    QElapsedTimer timer;
		    timer.start();
		    for (const auto& control: frames) {
			    if (control) qs::core::FrameAnimationDriver::batchFrame(control->window());
		    }
		    if (auto* driver = qs::core::FrameAnimationDriver::instance()) driver->advance();
		    if (frameTracingEnabled()) {
			    qCDebug(logFrame) << "batch" << frames.size() << "animation_us"
			                      << timer.nsecsElapsed() / 1000;
		    }
		    for (const auto& control: frames) {
			    if (!control) continue;
			    control->frameQueued = false;
			    control->renderFrame();
		    }
		    // A window held frames waited for may have drawn nothing.
		    commitHeldFrames();
		    qs::core::FrameAnimationDriver::framesDispatched();
	    },
	    Qt::QueuedConnection
	);
}

void WaylandRenderControl::renderFrame() {
	auto* window = this->window();
	if (!window->isExposed()) return;
	if (this->frameInFlight) {
		this->frameRequested = true;
		return;
	}
	// Allow the first frame with updates disabled; see QSGThreadedRenderLoop::handleExposure().
	if (this->presented && !QQuickWindowPrivate::get(window)->updatesEnabled) return;
	auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(window->handle());
	if (!waylandWindow) return;

	// The pixel needs neither the scene graph nor dmabuf formats.
	const auto inputOnly = this->inputOnly(waylandWindow);
	if (!inputOnly && !this->rhi && !this->initializeScene()) return;
	if (!inputOnly && !buffer::WlBufferManager::instance()->isReady()) return;

	QElapsedTimer timer;
	timer.start();
	this->polishing = true;
	const auto animating = qs::core::FrameAnimationDriver::frameStarted(window);
	this->polishItems();
	this->polishing = false;
	const auto polishNs = timer.nsecsElapsed();
	if (!window->isExposed()) return;
	if (this->inputOnly(waylandWindow)) {
		this->commitPixel(waylandWindow);
		return;
	}
	if (!this->rhi && !this->initializeScene()) return;
	if (!buffer::WlBufferManager::instance()->isReady()) return;
	// A window that showed the pixel renders its whole scene again.
	if (std::exchange(this->pixelAttached, false)) this->repaintRequested = true;

	const auto frame = std::make_shared<Frame>();
	frame->surfaceSize = waylandWindow->surfaceSize();
	frame->scale = waylandWindow->scale();
	frame->pixelRatio = window->effectiveDevicePixelRatio();
	frame->contentSize = waylandWindow->geometry().size() * frame->scale;
	frame->color = window->color();
	frame->revision = this->commitRevision;
	frame->serial = ++FRAME_SERIAL;
	frame->requested = std::exchange(this->requestedAt, 0);
	frame->input = std::exchange(this->inputAt, 0);
	frame->animating = animating;
	frame->polishNs = polishNs;
	const auto pixelSize = frame->surfaceSize * frame->scale;
	auto* decoration = waylandWindow->decoration();
	const auto alpha = decoration || window->requestedFormat().hasAlpha();
	if (pixelSize.isEmpty()) {
		this->commitWithoutFrame();
		return;
	}
	// onBufferReleased() schedules another frame.
	if (!this->pool.available(pixelSize, alpha)) {
		this->bufferAwaited = true;
		return;
	}
	frame->buffer = this->pool.acquire(pixelSize, alpha);
	if (!frame->buffer) {
		this->skipFrame();
		return;
	}
	if (decoration) {
		frame->decoration = decoration->contentImage();
		const auto margins = waylandWindow->clientSideMargins();
		frame->decorationOffset = (QPointF(margins.left(), margins.top()) * frame->scale).toPoint();
	}
	frame->repaint = std::exchange(this->repaintRequested, false);
	this->frameInFlight = true;
	this->frameRequested = false;
	frame->drawing = true;
	frame->screen = this->window()->screen();
	++frameSync().screens.tryEmplace(frame->screen).iterator->drawing;
	const auto acknowledge = std::exchange(this->damagePresented, false);
	timer.restart();
	this->renderThread->submit([this, frame, acknowledge] {
		QElapsedTimer timer;
		timer.start();
		if (acknowledge && this->tracker) this->tracker->presented();
		this->renderThread->checkGraphicsReset();
		if (!this->renderThread->deviceLost()) this->beginFrame();
		frame->failed = !this->rhi->isRecordingFrame();
		if (!frame->failed) this->sync();
		frame->syncNs = timer.nsecsElapsed();
		this->synchronized.release();
		if (!frame->failed) {
			this->advanceAnimators();
			this->drawFrame(*frame);
		}
		QMetaObject::invokeMethod(
		    this,
		    [this, frame, hold = qs::core::FrameAnimationDriver::holdIncubation()] {
			    this->finishFrame(frame);
		    },
		    Qt::QueuedConnection
		);
	});
	this->synchronized.acquire();
	frame->waitNs = timer.nsecsElapsed();
}

void WaylandRenderControl::advanceAnimators() { this->renderThread->advanceAnimators(); }

bool WaylandRenderControl::partialRepaint() const {
	static const bool disabled = qEnvironmentVariableIsSet("QS_NO_PARTIAL_REPAINT");
	// Only OpenGL: on Vulkan a buffer rendered with its contents kept came out wrong.
	if (disabled || this->rhi->backend() != QRhi::OpenGLES2) return false;
	// Without the depth buffer the renderer blends every node, so a transparent fill clears nothing.
	return this->window()->graphicsConfiguration().isDepthBufferEnabledFor2D();
}

// Qt Quick takes no clip from outside, so the scene goes under a clip node and the renderer
// scissors every batch. The unblended fill stands in for the clear a partial repaint skips.
bool WaylandRenderControl::attachClip() {
	auto* root = QQuickWindowPrivate::get(this->window())->renderer->rootNode();
	if (!this->sceneClip) this->sceneClip = std::make_unique<SceneClip>();
	auto& clip = *this->sceneClip;
	if (clip.scene.parent() == root) return false;

	while (auto* child = root->firstChild()) {
		root->removeChildNode(child);
		clip.scene.appendChildNode(child);
	}

	root->appendChildNode(&clip.fillClip);
	root->appendChildNode(&clip.scene);
	return true;
}

QRect WaylandRenderControl::clipRect(const Frame& frame, const QRegion& changed) {
	const auto* buffer = frame.buffer;
	if (!buffer->stale || !buffer->partialTarget) return {};
	// The fill writes premultiplied color, the clear it replaces the color as it is.
	if (frame.color.alpha() != 255 && frame.color.rgba() != 0) return {};

	const auto clip = (changed + *buffer->stale).boundingRect();
	// Heuristic: a clip over most of the buffer is drawn whole.
	const auto area = static_cast<qint64>(clip.width()) * clip.height();
	const auto whole = static_cast<qint64>(buffer->size.width()) * buffer->size.height();
	return area * 2 > whole ? QRect() : clip;
}

// The renderer reads the clip rects while it prepares the pass, so they need no dirty marks,
// which the damage tracker would count as damage.
void WaylandRenderControl::applyClip(const Frame& frame) {
	auto& clip = *this->sceneClip;
	const auto window = QRectF(QPointF(), frame.contentSize.toSizeF() / frame.pixelRatio);
	const auto rect = QRectF(
	    frame.clip.topLeft().toPointF() / frame.pixelRatio,
	    frame.clip.size().toSizeF() / frame.pixelRatio
	);

	clip.fillClip.setClipRect(frame.clip.isEmpty() ? QRectF() : rect);
	clip.scene.setClipRect(frame.clip.isEmpty() ? window : rect);

	if (clip.size != window.size()) {
		clip.size = window.size();
		QSGGeometry::updateRectGeometry(&clip.geometry, window);
		clip.fill.markDirty(QSGNode::DirtyGeometry);
	}

	if (clip.material.color() != frame.color) {
		clip.material.setColor(frame.color);
		// setColor() blends any color that is not opaque.
		clip.material.setFlag(QSGMaterial::Blending, false);
		clip.fill.markDirty(QSGNode::DirtyMaterial);
	}
}

// Animators and preprocess change the scene during the pass, after the clip was chosen; see
// QQuickAnimatorController::advance(). The other buffers lack those changes too.
void WaylandRenderControl::frameDrawn(const Frame& frame, bool tracked) {
	const auto size = frame.buffer->size;
	std::optional<QRegion> changed;
	if (tracked && frame.damage) changed = scaledRegion(*frame.damage, frame.pixelRatio, size);
	this->pool.drawn(frame.buffer, changed);
	if (frame.clip.isEmpty()) return;

	const auto unpainted = changed.value_or(QRect(QPoint(), size)) - frame.clip;
	this->tracker->damageAfterPresent(windowRegion(unpainted, frame.pixelRatio));
}

void WaylandRenderControl::drawFrame(Frame& frame) {
	QElapsedTimer timer;
	timer.start();
	auto readbackQueued = false;
	auto tracked = false;
	const auto finish = qScopeGuard([this, &frame, &timer, &readbackQueued, &tracked] {
		this->endFrame();
		// The buffer is committed without explicit sync. QRhiGles2::endOffscreenFrame() only
		// flushes, and as of Qt 6.12 QRhiVulkan::endOffscreenFrame() no longer waits for the GPU.
		if (frame.rendered) frame.fence = frame.buffer->buffer->createRenderFence(this->rhi);
		// The finished frame completed the readback.
		if (readbackQueued) frame.failed = !copyReadback(this->readback, frame.buffer->shm->image());
		// Collect changes made during render; WindowDamageTracker::sync() forces full damage with margins.
		frame.damage = this->tracker ? this->tracker->damage() : std::nullopt;
		if (frame.rendered && !frame.failed) this->frameDrawn(frame, tracked);
		frame.pendingDamage = this->tracker && this->tracker->pendingDamageAfterPresent();
		frame.renderNs = timer.nsecsElapsed();
	});
	const auto partial = this->partialRepaint();
	const auto wrapped = partial && this->attachClip();
	frame.damage = this->tracker ? this->tracker->damage() : std::nullopt;
	frame.rendered = frame.repaint || !this->tracker || frame.damage != QRegion()
	              || this->tracker->pendingPreprocess();
	if (!frame.rendered) return;
	if (frame.decoration.isNull()) this->decoration.reset();
	if (!this->pool.prepare(this->window(), this->rhi, frame.buffer, partial)) {
		frame.failed = true;
		return;
	}
	auto* target = frame.decoration.isNull()
	                 ? frame.buffer->renderTarget.get()
	                 : this->decorationTarget(frame.contentSize, frame.buffer->texture->format());
	if (!target) {
		frame.failed = true;
		return;
	}

	// Set when the damage holds every pixel the scene changed since the last frame. A decorated
	// buffer is offset from the window, and glyphs laid out during the pass are not measured yet.
	const auto measured = this->tracker && !this->tracker->pendingPreprocess();
	tracked = partial && !frame.repaint && !wrapped && measured && frame.damage
	       && frame.decoration.isNull();
	if (tracked) {
		const auto changed = scaledRegion(*frame.damage, frame.pixelRatio, frame.buffer->size);
		frame.clip = WaylandRenderControl::clipRect(frame, changed);
	}

	if (!frame.clip.isEmpty()) target = frame.buffer->partialTarget.get();
	if (this->sceneClip) this->applyClip(frame);

	auto renderTarget = QQuickRenderTarget::fromRhiRenderTarget(target);
	// Match the surface orientation when the framebuffer's Y axis points up.
	renderTarget.setMirrorVertically(this->rhi->isYUpInFramebuffer());
	this->window()->setRenderTarget(renderTarget);
	if (this->tracker) this->tracker->beginRender();
	this->render();
	if (!frame.decoration.isNull()) {
		frame.failed = !this->drawDecoration(frame.decoration, frame.buffer, frame.decorationOffset);
	}
	if (frame.failed) return;

	if (frame.buffer->shm) {
		this->readback = {};
		auto* updates = this->rhi->nextResourceUpdateBatch();
		updates->readBackTexture(QRhiReadbackDescription(frame.buffer->texture.get()), &this->readback);
		this->commandBuffer()->resourceUpdate(updates);
		readbackQueued = true;
		return;
	}

	frame.buffer->import->release(this->commandBuffer(), frame.buffer->texture.get());
}

void WaylandRenderControl::onFenceSignaled() {
	this->fenceNotifier.setEnabled(false);
	const auto frame = std::exchange(this->pendingFrame, nullptr);
	if (!frame) return;

	close(std::exchange(frame->fence, -1));
	this->finishFrame(frame);
}

void WaylandRenderControl::finishFrame(const std::shared_ptr<Frame>& frame) {
	// The GPU is still drawing, and the frame stays in flight until onFenceSignaled() finishes it.
	if (frame->fence != -1) {
		this->pendingFrame = frame;
		this->fenceNotifier.setSocket(frame->fence);
		this->fenceNotifier.setEnabled(true);
		return;
	}

	// Windows that animate together, like a popout and its frame chrome, commit together.
	auto& held = *frameSync().screens.tryEmplace(frame->screen).iterator;
	if (std::exchange(frame->drawing, false)) --held.drawing;
	if (held.frames.isEmpty()) held.heldSince = PresentationTracker::now();
	held.frames.append({this, frame});
	commitHeldFrames();
}

void WaylandRenderControl::completeFrame(const std::shared_ptr<Frame>& frame) {
	this->frameInFlight = false;
	auto* window = this->window();
	auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(window->handle());
	const auto valid = waylandWindow && window->isExposed() && !this->inputOnly(waylandWindow)
	                && frame->surfaceSize == waylandWindow->surfaceSize()
	                && frame->scale == waylandWindow->scale();

	if (frameTracingEnabled()) {
		qCDebug(logFrame) << "frame" << frame->serial << window << "polish_us" << frame->polishNs / 1000
		                  << "wait_us" << frame->waitNs / 1000 << "sync_us" << frame->syncNs / 1000
		                  << "render_us" << frame->renderNs / 1000 << "rendered" << frame->rendered
		                  << "clip" << frame->clip << "valid" << valid << "failed" << frame->failed;
	}

	if (!valid) {
		this->repaintRequested = true;
		if (std::exchange(this->frameRequested, false) || window->isExposed()) this->scheduleFrame();
		return;
	}

	auto committed = false;
	if (frame->failed) {
		this->repaintRequested = true;
		this->skipFrame();
	} else if (frame->rendered) {
		committed = this->commitFrame(*frame, waylandWindow);
	} else {
		this->commitWithoutFrame();
	}

	// Emit frameSwapped even when damage tracking skips the render.
	QQuickWindowPrivate::get(window)->fireFrameSwapped();
	// Only a committed buffer brings a frame callback; an unchanged window waits for the fallback instead of polling.
	const auto animating = frame->animating && frame->rendered && !frame->failed;
	const auto redraw = committed && frame->pendingDamage;
	if (std::exchange(this->frameRequested, false) || redraw || animating) {
		this->scheduleFrame();
	}
}

bool WaylandRenderControl::commitFrame(
    const Frame& frame,
    QtWaylandClient::QWaylandWindow* waylandWindow
) {
	PresentationFeedback* feedback = nullptr;
	if (this->presentation) {
		feedback = this->presentation
		               ->track(waylandWindow->surface(), frame.serial, frame.requested, frame.input);
	}

	auto ref = WlBufferRef(frame.buffer->buffer->buffer(), frame.surfaceSize);
	// QWaylandWindow::commit() takes surface coordinates, the damage is in logical ones.
	const auto surfaceScale = frame.pixelRatio / frame.scale;
	auto damage = QRegion(QRect(QPoint(), frame.surfaceSize));
	if (frame.damage) damage = scaledRegion(*frame.damage, surfaceScale, frame.surfaceSize);
	waylandWindow->safeCommit(&ref, damage);
	// Keep the damage until safeCommit() accepts a buffer.
	if (!ref.busy()) {
		PresentationTracker::cancel(feedback);
		return false;
	}

	frame.buffer->busy = true;
	this->presented = true;
	this->frameFailing = false;
	if (frame.revision == this->commitRevision) this->commitRequested = false;
	this->damagePresented = true;
	emit this->committed(true);
	return true;
}

bool WaylandRenderControl::inputOnly(QtWaylandClient::QWaylandWindow* waylandWindow) const {
	auto* proxied = qobject_cast<ProxiedWindow*>(this->window());
	if (!proxied || !proxied->proxy() || !proxied->proxy()->inputOnly()) return false;

	// Qt scales buffers through a viewport only under both globals; see QWaylandWindow::initWindow().
	auto* display = waylandWindow->display();
	if (display->viewporter() && display->fractionalScaleManager()) return true;

	static auto logged = false;
	if (!std::exchange(logged, true)) {
		qCInfo(logRenderControl) << "Input only windows render normally without wp_viewporter.";
	}

	return false;
}

// One transparent pixel scaled to the surface by QWaylandWindow's viewport, so no surface sized buffer is kept.
void WaylandRenderControl::commitPixel(QtWaylandClient::QWaylandWindow* waylandWindow) {
	if (!this->pixel) {
		buffer::WlBufferRequest request;
		request.width = 1;
		request.height = 1;
		request.shm.formats.push(WL_SHM_FORMAT_ARGB8888);
		this->pixel.reset(buffer::shm::ShmbufManager::createShmbuf(request));
	}

	if (!this->pixel) {
		qCWarning(logRenderControl) << "Failed to create the input only buffer for" << this->window();
		this->commitWithoutFrame();
		return;
	}

	// The scene buffers are released like those of a hidden window.
	if (!this->pixelAttached) {
		this->pool.forget();
		this->trimTimer.start();
	}

	auto ref = WlBufferRef(this->pixel->buffer(), this->pixel->size());
	waylandWindow->safeCommit(&ref, QRect(QPoint(), ref.size()));
	QQuickWindowPrivate::get(this->window())->fireFrameSwapped();
	if (!ref.busy()) return;

	this->commitRequested = false;
	this->presented = true;
	this->pixelAttached = true;
	emit this->committed(true);
}

void WaylandRenderControl::skipFrame() {
	this->commitWithoutFrame();
	if (!this->renderThread->deviceLost()) {
		if (!std::exchange(this->frameFailing, true)) {
			qCWarning(logRenderControl) << "Failed to render a frame for" << this->window();
		}
		return;
	}

	qCWarning(logRenderControl) << "Graphics device lost for" << this->window()
	                            << "- rebuilding the scene";
	this->recoveringDevice = true;
	this->releaseScene();
	this->scheduleFrame();
}

void WaylandRenderControl::commitWithoutFrame() {
	if (!this->commitRequested) return;
	this->commitRequested = false;

	if (auto* window = dynamic_cast<QtWaylandClient::QWaylandWindow*>(this->window()->handle())) {
		window->commit();
		emit this->committed(false);
	}
}

// drawDecoration() copies texels as they are stored, so its textures share the buffer's format.
QRhiRenderTarget* WaylandRenderControl::decorationTarget(QSize size, QRhiTexture::Format format) {
	if (this->decoration && this->decoration->size == size) {
		return this->decoration->renderTarget.get();
	}

	auto target = std::make_unique<DecorationTarget>();
	target->size = size;
	target->content.reset(this->rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget));
	target->depthStencil.reset(this->rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size));
	// QRhiVulkan builds the render pass from the formats of created attachments.
	if (!target->content->create() || !target->depthStencil->create()) return nullptr;

	auto description = QRhiTextureRenderTargetDescription(QRhiColorAttachment(target->content.get()));
	description.setDepthStencilBuffer(target->depthStencil.get());
	target->renderTarget.reset(this->rhi->newTextureRenderTarget(description));
	target->renderPass.reset(target->renderTarget->newCompatibleRenderPassDescriptor());
	target->renderTarget->setRenderPassDescriptor(target->renderPass.get());
	if (!target->renderTarget->create()) return nullptr;

	this->decoration = std::move(target);
	return this->decoration->renderTarget.get();
}

// Replace the decoration's content area, including transparent pixels.
bool WaylandRenderControl::drawDecoration(
    const QImage& image,
    SurfaceBuffer* buffer,
    QPoint offset
) {
	auto* target = this->decoration.get();

	const auto format = buffer->texture->format();
	if (!target->decoration || target->decoration->pixelSize() != image.size()) {
		target->decoration.reset(this->rhi->newTexture(format, image.size()));
		target->decorationKey = 0;
		if (!target->decoration->create()) {
			target->decoration.reset();
			return false;
		}
	}

	auto* updates = this->rhi->nextResourceUpdateBatch();
	if (image.cacheKey() != target->decorationKey) {
		target->decorationKey = image.cacheKey();
		auto imageFormat = QImage::Format_RGBA8888_Premultiplied;
		if (format == QRhiTexture::BGRA8) imageFormat = QImage::Format_ARGB32_Premultiplied;
		const auto converted = image.convertToFormat(imageFormat);
		updates->uploadTexture(target->decoration.get(), converted);
	}

	const auto bufferRect = QRect(QPoint(), buffer->size);

	QRhiTextureCopyDescription frame;
	frame.setPixelSize(QRect(QPoint(), image.size()).intersected(bufferRect).size());
	updates->copyTexture(buffer->texture.get(), target->decoration.get(), frame);

	QRhiTextureCopyDescription content;
	content.setDestinationTopLeft(offset);
	content.setPixelSize(QRect(offset, target->size).intersected(bufferRect).size());
	updates->copyTexture(buffer->texture.get(), target->content.get(), content);

	this->commandBuffer()->resourceUpdate(updates);
	return true;
}

} // namespace qs::wayland::render

// NOLINTNEXTLINE(misc-use-internal-linkage)
void installRenderControl() { qs::wayland::render::install(); }
