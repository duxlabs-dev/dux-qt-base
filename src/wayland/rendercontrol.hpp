#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <qcolor.h>
#include <qcoreevent.h>
#include <qhash.h>
#include <qimage.h>
#include <qlist.h>
#include <qobject.h>
#include <qpoint.h>
#include <qpointer.h>
#include <qquickwindow.h>
#include <qregion.h>
#include <qscreen.h>
#include <qsemaphore.h>
#include <qsize.h>
#include <qsocketnotifier.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <rhi/qrhi.h>
#include <wayland-client-protocol.h>

#include "../window/rendercontrol.hpp"
#include "buffer/manager.hpp"
#include "buffer/qsg.hpp"

namespace qs::window {
class WindowDamageTracker;
}

namespace qs::wayland::buffer::shm {
class WlShmTarget;
}

namespace QtWaylandClient { // NOLINT(readability-identifier-naming)
class QWaylandWindow;
}

namespace qs::wayland::render {

class SurfaceBufferPool;
class RenderThread;
struct SceneClip;
class PresentationTracker;
class WaylandRenderControl;

class SurfaceBuffer {
public:
	SurfaceBuffer() = default;
	~SurfaceBuffer();
	Q_DISABLE_COPY_MOVE(SurfaceBuffer);

	SurfaceBufferPool* pool = nullptr;
	QSize size;
	bool alpha = false;
	// Set by renderFrame(), cleared by SurfaceBufferPool::onRelease().
	bool busy = false;
	// Keep the dmabuf alive until the GL objects are destroyed.
	std::unique_ptr<buffer::WlBuffer> buffer;
	// Set when buffer is a shm fallback; drawFrame() reads texture back into it.
	buffer::shm::WlShmTarget* shm = nullptr;
	std::unique_ptr<buffer::WlBufferQSGTexture> import;
	std::unique_ptr<QRhiTexture> texture;
	std::shared_ptr<QRhiRenderBuffer> depthStencil;
	std::unique_ptr<QRhiRenderPassDescriptor> renderPass;
	std::unique_ptr<QRhiTextureRenderTarget> renderTarget;
	// Keeps the color contents so drawFrame() can repaint only the damage.
	std::unique_ptr<QRhiRenderPassDescriptor> partialPass;
	std::unique_ptr<QRhiTextureRenderTarget> partialTarget;
	// Render thread only. Pixels the scene changed since this was drawn; unset until it holds a frame.
	std::optional<QRegion> stale;
};

class SurfaceBufferPool {
public:
	explicit SurfaceBufferPool(WaylandRenderControl* control): control(control) {}

	[[nodiscard]] bool available(QSize size, bool alpha) const;
	// Requires available(size, alpha).
	SurfaceBuffer* acquire(QSize size, bool alpha);
	bool prepare(QQuickWindow* window, QRhi* rhi, SurfaceBuffer* buffer, bool partial);
	// Render thread, frame in flight. `changed` is unset when the whole frame changed.
	void drawn(SurfaceBuffer* buffer, const std::optional<QRegion>& changed);
	void clear();
	// Drops idle buffers other than the last acquired one.
	bool trim();
	// Lets trim() drop the last acquired buffer too.
	void forget() { this->current = nullptr; }

private:
	friend class SurfaceBuffer;
	static void onRelease(void* data, wl_buffer* buffer);
	static const wl_buffer_listener LISTENER;

	SurfaceBuffer* create(QSize size, bool alpha);

	std::vector<std::unique_ptr<SurfaceBuffer>> buffers;
	std::shared_ptr<QRhiRenderBuffer> depthStencil;
	SurfaceBuffer* current = nullptr;
	WaylandRenderControl* control;
};

// Separate content target for drawDecoration().
struct DecorationTarget {
	QSize size;
	std::unique_ptr<QRhiTexture> content;
	std::unique_ptr<QRhiRenderBuffer> depthStencil;
	std::unique_ptr<QRhiRenderPassDescriptor> renderPass;
	std::unique_ptr<QRhiTextureRenderTarget> renderTarget;
	std::unique_ptr<QRhiTexture> decoration;
	qint64 decorationKey = 0;
};

class WaylandRenderControl: public qs::window::WindowRenderControl {
	Q_OBJECT;

public:
	WaylandRenderControl();
	~WaylandRenderControl() override;
	Q_DISABLE_COPY_MOVE(WaylandRenderControl);

	void requestCommit() override;
	bool eventFilter(QObject* watched, QEvent* event) override;
	void onBufferReleased();
	void releaseBuffer(SurfaceBuffer* buffer);
	void releaseDepthStencil(std::shared_ptr<QRhiRenderBuffer>& buffer);

private slots:
	void scheduleFrame();
	void onRenderRequested();
	void onFenceSignaled();
	void trimBuffers();

private:
	struct Frame {
		Frame() = default;
		~Frame();
		Q_DISABLE_COPY_MOVE(Frame);

		SurfaceBuffer* buffer = nullptr;
		// Signals once the GPU has drawn the frame, -1 when it has; see WlBuffer::createRenderFence().
		int fence = -1;
		QSize surfaceSize;
		QSize contentSize;
		qreal scale = 1;
		// Buffer pixels per logical pixel; scale is per surface unit and leaves out QT_SCALE_FACTOR.
		qreal pixelRatio = 1;
		QImage decoration;
		QPoint decorationOffset;
		QColor color;
		std::optional<QRegion> damage;
		// Buffer pixels the pass was clipped to, empty when it drew the whole buffer.
		QRect clip;
		quint64 revision = 0;
		quint64 serial = 0;
		qint64 requested = 0;
		qint64 input = 0;
		bool repaint = false;
		bool animating = false;
		bool rendered = false;
		bool failed = false;
		bool pendingDamage = false;
		// Counted in its screen's FrameSync until finishFrame() takes it out.
		bool drawing = false;
		QScreen* screen = nullptr;
		qint64 polishNs = 0;
		qint64 syncNs = 0;
		qint64 waitNs = 0;
		qint64 renderNs = 0;
	};

	using HeldFrames = QList<std::pair<QPointer<WaylandRenderControl>, std::shared_ptr<Frame>>>;

	// Finished frames on a screen wait while another frame on it draws, then all commit at once.
	struct FrameSync {
		struct Screen {
			qsizetype drawing = 0;
			HeldFrames frames;
			qint64 heldSince = 0;
		};

		QHash<QScreen*, Screen> screens;
		QList<WaylandRenderControl*> controls;
		QTimer* timer = nullptr;
	};

	static FrameSync& frameSync();
	static void commitHeldFrames();
	static void queueCommitHeldFrames();
	bool initializeScene();
	void releaseScene();
	[[nodiscard]] bool inputOnly(QtWaylandClient::QWaylandWindow* waylandWindow) const;
	void commitPixel(QtWaylandClient::QWaylandWindow* waylandWindow);
	void queueFrame();
	void renderFrame();
	void advanceAnimators();
	[[nodiscard]] bool partialRepaint() const;
	// Returns whether the scene was just wrapped, which damages all of it.
	bool attachClip();
	// Buffer pixels to repaint, empty for the whole buffer.
	[[nodiscard]] static QRect clipRect(const Frame& frame, const QRegion& changed);
	void applyClip(const Frame& frame);
	// `tracked` is set when the damage held every pixel the scene changed before the pass.
	void frameDrawn(const Frame& frame, bool tracked);
	void drawFrame(Frame& frame);
	void finishFrame(const std::shared_ptr<Frame>& frame);
	void completeFrame(const std::shared_ptr<Frame>& frame);
	bool commitFrame(const Frame& frame, QtWaylandClient::QWaylandWindow* waylandWindow);
	void skipFrame();
	void commitWithoutFrame();
	QRhiRenderTarget* decorationTarget(QSize size, QRhiTexture::Format format);
	bool drawDecoration(const QImage& image, SurfaceBuffer* buffer, QPoint offset);

	std::unique_ptr<PresentationTracker> presentation;
	std::shared_ptr<RenderThread> renderThread;
	QSemaphore synchronized;
	QRhi* rhi = nullptr;
	SurfaceBufferPool pool;
	std::unique_ptr<DecorationTarget> decoration;
	qs::window::WindowDamageTracker* tracker = nullptr;
	std::unique_ptr<buffer::WlBuffer> pixel;
	// Render thread only. Outlives the QRhi, which may complete a readback after its frame failed.
	QRhiReadbackResult readback;
	// Render thread only. Set while partial repaint has the scene wrapped; see attachClip().
	std::unique_ptr<SceneClip> sceneClip;
	QTimer trimTimer;
	// Waits for the fence of pendingFrame, which finishFrame() then commits.
	QSocketNotifier fenceNotifier {QSocketNotifier::Read};
	std::shared_ptr<Frame> pendingFrame;
	bool bufferAwaited = false;
	bool pixelAttached = false;
	bool commitRequested = false;
	bool presented = false;
	bool repaintRequested = false;
	bool polishing = false;
	bool frameQueued = false;
	bool frameInFlight = false;
	bool frameRequested = false;
	bool damagePresented = false;
	bool recoveringDevice = false;
	bool frameFailing = false;
	quint64 commitRevision = 0;
	qint64 requestedAt = 0;
	qint64 inputAt = 0;
	// When an update was asked for that has not arrived yet.
	qint64 awaitingSince = 0;
};

} // namespace qs::wayland::render
