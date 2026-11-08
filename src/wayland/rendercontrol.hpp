#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <qcoreevent.h>
#include <qimage.h>
#include <qobject.h>
#include <qpoint.h>
#include <qquickwindow.h>
#include <qregion.h>
#include <qsize.h>
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

namespace QtWaylandClient {
class QWaylandWindow;
}

namespace qs::wayland::render {

class SurfaceBufferPool;
class RenderThread;
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
	std::unique_ptr<buffer::WlBufferQSGTexture> import;
	std::unique_ptr<QRhiTexture> texture;
	std::shared_ptr<QRhiRenderBuffer> depthStencil;
	std::unique_ptr<QRhiRenderPassDescriptor> renderPass;
	std::unique_ptr<QRhiTextureRenderTarget> renderTarget;
};

class SurfaceBufferPool {
public:
	explicit SurfaceBufferPool(WaylandRenderControl* control): control(control) {}

	[[nodiscard]] bool available(QSize size, bool alpha) const;
	// Requires available(size, alpha).
	SurfaceBuffer* acquire(QSize size, bool alpha);
	bool prepare(QQuickWindow* window, QRhi* rhi, SurfaceBuffer* buffer);
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
	void trimBuffers();

private:
	struct Frame {
		SurfaceBuffer* buffer = nullptr;
		QSize surfaceSize;
		QSize contentSize;
		qreal scale = 1;
		QImage decoration;
		QPoint decorationOffset;
		std::optional<QRegion> damage;
		quint64 revision = 0;
		quint64 serial = 0;
		qint64 requested = 0;
		qint64 input = 0;
		bool repaint = false;
		bool animating = false;
		bool rendered = false;
		bool failed = false;
		bool pendingDamage = false;
		qint64 polishNs = 0;
		qint64 syncNs = 0;
		qint64 renderNs = 0;
	};

	bool initializeScene();
	[[nodiscard]] bool inputOnly(QtWaylandClient::QWaylandWindow* waylandWindow) const;
	void commitPixel(QtWaylandClient::QWaylandWindow* waylandWindow);
	void renderFrame();
	void drawFrame(Frame& frame);
	void finishFrame(const std::shared_ptr<Frame>& frame);
	void commitFrame(Frame& frame, QtWaylandClient::QWaylandWindow* waylandWindow);
	void abortFrame();
	void commitWithoutFrame();
	QRhiRenderTarget* decorationTarget(QSize size);
	bool drawDecoration(const QImage& image, SurfaceBuffer* buffer, QPoint offset);

	std::unique_ptr<PresentationTracker> presentation;
	std::shared_ptr<RenderThread> renderThread;
	QRhi* rhi = nullptr;
	SurfaceBufferPool pool;
	std::unique_ptr<DecorationTarget> decoration;
	qs::window::WindowDamageTracker* tracker = nullptr;
	std::unique_ptr<buffer::WlBuffer> pixel;
	QTimer trimTimer;
	bool bufferAwaited = false;
	bool pixelAttached = false;
	bool commitRequested = false;
	bool presented = false;
	bool repaintRequested = false;
	bool polishing = false;
	bool frameInFlight = false;
	bool frameRequested = false;
	quint64 commitRevision = 0;
	qint64 requestedAt = 0;
	qint64 inputAt = 0;
};

} // namespace qs::wayland::render
