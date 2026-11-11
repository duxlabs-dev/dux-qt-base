#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <private/qwaylandshmbackingstore_p.h>
#include <qimage.h>
#include <qquickwindow.h>
#include <qsgtexture.h>
#include <qsize.h>
#include <qtclasshelpermacros.h>
#include <wayland-client-protocol.h>

#include "manager.hpp"
#include "qsg.hpp"

namespace qs::wayland::buffer::shm {

class WlShmBuffer: public WlBuffer {
public:
	~WlShmBuffer() override;
	Q_DISABLE_COPY_MOVE(WlShmBuffer);

	[[nodiscard]] wl_buffer* buffer() const override { return this->shmBuffer->buffer(); }
	[[nodiscard]] QSize size() const override { return this->shmBuffer->size(); }
	[[nodiscard]] bool isCompatible(const WlBufferRequest& request) const override;
	[[nodiscard]] WlBufferQSGTexture* createQsgTexture(QQuickWindow* window) const override;

private:
	WlShmBuffer(QtWaylandClient::QWaylandShmBuffer* shmBuffer, uint32_t format)
	    : shmBuffer(shmBuffer)
	    , format(format) {}

	std::shared_ptr<QtWaylandClient::QWaylandShmBuffer> shmBuffer;
	uint32_t format;

	friend class WlShmBufferQSGTexture;
	friend class ShmbufManager;
	friend QDebug& operator<<(QDebug& debug, const WlShmBuffer* buffer);
};

QDebug& operator<<(QDebug& debug, const WlShmBuffer* buffer);

class WlShmBufferQSGTexture: public WlBufferQSGTexture {
public:
	[[nodiscard]] QSGTexture* texture() const override { return this->qsgTexture.get(); }
	void sync(const WlBuffer* buffer, QQuickWindow* window) override;

private:
	WlShmBufferQSGTexture() = default;

	std::shared_ptr<QtWaylandClient::QWaylandShmBuffer> shmBuffer;
	std::unique_ptr<QSGTexture> qsgTexture;

	friend class WlShmBuffer;
};

class ShmbufManager {
public:
	[[nodiscard]] static WlBuffer* createShmbuf(const WlBufferRequest& request);
};

// Unlike QWaylandShmBuffer's, the wl_buffer has no listener, so the owner can track releases.
class WlShmTarget: public WlBuffer {
public:
	~WlShmTarget() override;
	Q_DISABLE_COPY_MOVE(WlShmTarget);

	[[nodiscard]] static WlShmTarget* create(QSize size, uint32_t format);

	[[nodiscard]] wl_buffer* buffer() const override { return this->mBuffer; }
	[[nodiscard]] QSize size() const override { return this->mImage.size(); }
	[[nodiscard]] bool isCompatible(const WlBufferRequest& request) const override;
	// Written by the CPU only, never sampled.
	[[nodiscard]] WlBufferQSGTexture* createQsgTexture(QQuickWindow* window) const override;

	[[nodiscard]] QImage* image() { return &this->mImage; }

private:
	WlShmTarget() = default;

	wl_buffer* mBuffer = nullptr;
	void* memory = nullptr;
	size_t length = 0;
	uint32_t format = 0;
	// Wraps memory.
	QImage mImage;
};

} // namespace qs::wayland::buffer::shm
