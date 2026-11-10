#include "rendercontrol.hpp"
#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include <libdrm/drm_fourcc.h>
#include <private/qquickanimatorcontroller_p.h>
#include <private/qquickwindow_p.h>
#include <private/qwaylandabstractdecoration_p.h>
#include <private/qwaylandbuffer_p.h>
#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandwindow_p.h>
#include <qabstractanimation.h>
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
#include <qpoint.h>
#include <qpointer.h>
#include <qquickgraphicsdevice.h>
#include <qquickrendertarget.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qregion.h>
#include <qscopeguard.h>
#include <qsemaphore.h>
#include <qsize.h>
#include <qstring.h>
#include <qsurfaceformat.h>
#include <qtclasshelpermacros.h>
#include <qtenvironmentvariables.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <rhi/qrhi.h>
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

qs::window::WindowRenderControl* createRenderControl() { return new WaylandRenderControl(); }

void install() {
	if (qEnvironmentVariableIsSet("QS_DISABLE_DMABUF")) {
		qCInfo(logRenderControl) << "Render control is unavailable with QS_DISABLE_DMABUF.";
		return;
	}

	qs::window::WindowRenderControl::setFactory(&createRenderControl);
}

} // namespace

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
	auto idle = [this](const auto& buffer) { return buffer.get() != this->current && !buffer->busy; };
	auto dropped = std::erase_if(this->buffers, idle) != 0;
	if (this->buffers.empty()) this->control->releaseDepthStencil(this->depthStencil);
	return dropped;
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
	if (!result->buffer) return nullptr;
	wl_buffer_add_listener(result->buffer->buffer(), &LISTENER, result.get());

	if (frameTracingEnabled()) qCDebug(logFrame) << "allocate" << size;
	return this->buffers.emplace_back(std::move(result)).get();
}

bool SurfaceBufferPool::prepare(QQuickWindow* window, QRhi* rhi, SurfaceBuffer* buffer) {
	if (buffer->renderTarget) return true;
	const auto size = buffer->size;
	buffer->import.reset(buffer->buffer->createQsgTexture(window));
	if (!buffer->import) return false;

	// The image's own format: QRhiVulkan creates the view with it.
	auto* imported = buffer->import->texture()->rhiTexture();
	const auto native = imported->nativeTexture();
	buffer->texture.reset(rhi->newTexture(imported->format(), size, 1, QRhiTexture::RenderTarget));
	if (!buffer->texture->createFrom({.object = native.object, .layout = 0})) return false;

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
	if (buffer->renderTarget->create()) return true;
	buffer->renderTarget.reset();
	return false;
}

WaylandRenderControl::WaylandRenderControl(): pool(this) {
	this->trimTimer.setSingleShot(true);
	this->trimTimer.setInterval(std::chrono::seconds(1));

	// clang-format off
	QObject::connect(this, &QQuickRenderControl::renderRequested, this, &WaylandRenderControl::onRenderRequested);
	QObject::connect(this, &QQuickRenderControl::sceneChanged, this, &WaylandRenderControl::scheduleFrame);
	QObject::connect(&this->trimTimer, &QTimer::timeout, this, &WaylandRenderControl::trimBuffers);
	// clang-format on
}

WaylandRenderControl::~WaylandRenderControl() { this->releaseScene(); }

// initializeScene() builds the scene and its thread again.
void WaylandRenderControl::releaseScene() {
	if (!this->renderThread || !this->rhi) return;

	this->renderThread->invoke([this] {
		this->rhi->makeThreadLocalNativeContextCurrent();
		// Invalidate before releasing rhi; QQuickRenderControl::~QQuickRenderControl() is too late.
		this->invalidate();
		this->decoration.reset();
		this->prepareThread(this->thread());
	});
	this->pool.clear();
	this->rhi = nullptr;
	this->renderThread.reset();
}

void WaylandRenderControl::releaseBuffer(SurfaceBuffer* buffer) {
	if (!this->renderThread) return;
	this->renderThread->invoke([this, buffer] {
		// WlDmaBufferQSGTexture's destructor needs the current context.
		if (this->rhi) this->rhi->makeThreadLocalNativeContextCurrent();
		buffer->renderTarget.reset();
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
	if (window->isExposed()) qs::core::FrameAnimationDriver::requestFrame(window);
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
	if (!this->pool.trim()) return;

	// QRhiGles2 frees released GL objects in beginOffscreenFrame(), QRhiVulkan only those of
	// one frame slot there and the rest in finish(); see executeDeferredReleases().
	this->renderThread->invoke([this] {
		if (this->rhi->backend() == QRhi::Vulkan) {
			this->rhi->finish();
			return;
		}

		QRhiCommandBuffer* commands = nullptr;
		if (this->rhi->beginOffscreenFrame(&commands) != QRhi::FrameOpSuccess) return;
		this->rhi->endOffscreenFrame();
	});
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
	case QEvent::UpdateRequest: this->queueFrame(); break;
	case QEvent::Expose:
		if (!this->window()->isExposed()) {
			this->presented = false;
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
	this->renderThread = std::make_unique<RenderThread>(window);
	this->rhi = this->renderThread->rhi();
	auto initialized = false;
	if (this->rhi) {
		window->setGraphicsDevice(QQuickGraphicsDevice::fromRhi(this->rhi));
		this->prepareThread(this->renderThread->thread());
		this->renderThread->invoke([this, window, manager, &initialized] {
			initialized = this->initialize();
			if (initialized) {
				manager->initWindow(window);
				return;
			}
			this->invalidate();
			this->prepareThread(this->thread());
		});
	}

	if (!initialized) {
		this->rhi = nullptr;
		this->renderThread.reset();
		emit window->sceneGraphError(
		    QQuickWindow::ContextNotAvailable,
		    QStringLiteral("Failed to initialize the render control.")
		);
		return false;
	}

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
	frame->contentSize = window->size() * frame->scale;
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
		this->abortFrame();
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
	const auto acknowledge = std::exchange(this->damagePresented, false);
	this->renderThread->submit([this, frame, acknowledge] {
		QElapsedTimer timer;
		timer.start();
		if (acknowledge && this->tracker) this->tracker->presented();
		this->beginFrame();
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
}

// Animator types run on the render thread; see QSGRenderThread::syncAndRender().
void WaylandRenderControl::advanceAnimators() {
	auto* driver = this->renderThread->animatorDriver();
	if (!driver || !driver->isRunning()) return;

	auto* controller = QQuickWindowPrivate::get(this->window())->animationController.get();
	controller->lock();
	driver->advance();
	controller->unlock();
}

void WaylandRenderControl::drawFrame(Frame& frame) {
	QElapsedTimer timer;
	timer.start();
	const auto finish = qScopeGuard([this, &frame, &timer] {
		this->endFrame();
		// As of Qt 6.12 QRhiVulkan::endOffscreenFrame() no longer waits for the GPU, and the
		// buffer is committed without explicit sync.
		if (frame.rendered && this->rhi->backend() == QRhi::Vulkan) this->rhi->finish();
		// Collect changes made during render; WindowDamageTracker::sync() forces full damage with margins.
		frame.damage = this->tracker ? this->tracker->damage() : std::nullopt;
		frame.pendingDamage = this->tracker && this->tracker->pendingDamageAfterPresent();
		frame.renderNs = timer.nsecsElapsed();
	});
	frame.damage = this->tracker ? this->tracker->damage() : std::nullopt;
	frame.rendered = frame.repaint || !this->tracker || frame.damage != QRegion()
	              || this->tracker->pendingPreprocess();
	if (!frame.rendered) return;
	if (frame.decoration.isNull()) this->decoration.reset();
	if (!this->pool.prepare(this->window(), this->rhi, frame.buffer)) {
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
	frame.buffer->import->release(this->commandBuffer(), frame.buffer->texture.get());
}

void WaylandRenderControl::finishFrame(const std::shared_ptr<Frame>& frame) {
	this->frameInFlight = false;
	auto* window = this->window();
	auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(window->handle());
	const auto valid = waylandWindow && window->isExposed() && !this->inputOnly(waylandWindow)
	                && frame->surfaceSize == waylandWindow->surfaceSize()
	                && frame->scale == waylandWindow->scale();

	if (frameTracingEnabled()) {
		qCDebug(logFrame) << "frame" << frame->serial << window << "polish_us" << frame->polishNs / 1000
		                  << "sync_us" << frame->syncNs / 1000 << "render_us" << frame->renderNs / 1000
		                  << "rendered" << frame->rendered << "valid" << valid << "failed"
		                  << frame->failed;
	}

	if (!valid) {
		this->repaintRequested = true;
		if (std::exchange(this->frameRequested, false) || window->isExposed()) this->scheduleFrame();
		return;
	}

	auto committed = false;
	if (frame->failed) {
		this->repaintRequested = true;
		this->abortFrame();
		// abortFrame() hid the window. A lost device stays lost, so showing it again needs a new one.
		if (this->rhi->isDeviceLost()) this->releaseScene();
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
	waylandWindow->safeCommit(&ref, frame.damage.value_or(QRect(QPoint(), frame.surfaceSize)));
	// Keep the damage until safeCommit() accepts a buffer.
	if (!ref.busy()) {
		PresentationTracker::cancel(feedback);
		return false;
	}

	frame.buffer->busy = true;
	this->presented = true;
	if (frame.revision == this->commitRevision) this->commitRequested = false;
	this->damagePresented = true;
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
}

void WaylandRenderControl::abortFrame() {
	this->commitWithoutFrame();
	emit this->window()->sceneGraphError(
	    QQuickWindow::ContextNotAvailable,
	    QStringLiteral("Failed to render a frame for the window.")
	);
}

void WaylandRenderControl::commitWithoutFrame() {
	if (!this->commitRequested) return;
	this->commitRequested = false;

	if (auto* window = dynamic_cast<QtWaylandClient::QWaylandWindow*>(this->window()->handle())) {
		window->commit();
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

	auto bufferRect = QRect(QPoint(), buffer->size);

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
