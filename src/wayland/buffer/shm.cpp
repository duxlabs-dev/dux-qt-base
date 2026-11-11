#include "shm.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandintegration_p.h>
#include <private/qwaylandshm_p.h>
#include <private/qwaylandshmbackingstore_p.h>
#include <qdebug.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qquickwindow.h>
#include <qscopeguard.h>
#include <qsize.h>
#include <qtypes.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "../../core/logcat.hpp"
#include "manager.hpp"

namespace qs::wayland::buffer::shm {

namespace {
QS_LOGGING_CATEGORY(logShm, "quickshell.wayland.buffer.shm", QtWarningMsg);
}

bool WlShmBuffer::isCompatible(const WlBufferRequest& request) const {
	if (QSize(static_cast<int>(request.width), static_cast<int>(request.height)) != this->size()) {
		return false;
	}

	auto matchingFormat = std::ranges::find(request.shm.formats, this->format);
	return matchingFormat != request.shm.formats.end();
}

QDebug& operator<<(QDebug& debug, const WlShmBuffer* buffer) {
	auto saver = QDebugStateSaver(debug);
	debug.nospace();

	if (buffer) {
		auto fmt = QtWaylandClient::QWaylandShm::formatFrom(
		    static_cast<::wl_shm_format>(buffer->format) // NOLINT
		);

		debug << "WlShmBuffer(" << static_cast<const void*>(buffer) << ", size=" << buffer->size()
		      << ", format=" << fmt << ')';
	} else {
		debug << "WlShmBuffer(0x0)";
	}

	return debug;
}

WlShmBuffer::~WlShmBuffer() { qCDebug(logShm) << "Destroyed" << this; }

WlBufferQSGTexture* WlShmBuffer::createQsgTexture(QQuickWindow* window) const {
	auto* texture = new WlShmBufferQSGTexture();

	// If the QWaylandShmBuffer is destroyed before the QSGTexture, we'll hit a UAF
	// in the render thread.
	texture->shmBuffer = this->shmBuffer;

	texture->qsgTexture.reset(window->createTextureFromImage(*this->shmBuffer->image()));
	texture->sync(this, window);
	return texture;
}

void WlShmBufferQSGTexture::sync(const WlBuffer* /*unused*/, QQuickWindow* window) {
	// This is both dumb and expensive. We should use an RHI texture and render images into
	// it more intelligently, but shm buffers are already a horribly slow fallback path,
	// to the point where it barely matters.
	this->qsgTexture.reset(window->createTextureFromImage(*this->shmBuffer->image()));
}

WlBuffer* ShmbufManager::createShmbuf(const WlBufferRequest& request) {
	if (request.shm.formats.isEmpty()) return nullptr;

	static const auto* waylandIntegration = QtWaylandClient::QWaylandIntegration::instance();
	auto* display = waylandIntegration->display();

	// Its probably fine...
	auto format = request.shm.formats[0];

	auto* buffer = new WlShmBuffer(
	    new QtWaylandClient::QWaylandShmBuffer(
	        display,
	        QSize(static_cast<int>(request.width), static_cast<int>(request.height)),
	        QtWaylandClient::QWaylandShm::formatFrom(static_cast<::wl_shm_format>(format)) // NOLINT
	    ),
	    format
	);

	qCDebug(logShm) << "Created shmbuf" << buffer;
	return buffer;
}

WlShmTarget::~WlShmTarget() {
	if (this->mBuffer) wl_buffer_destroy(this->mBuffer); // NOLINT(misc-include-cleaner)
	// Drop the image before the memory it wraps.
	this->mImage = QImage();
	if (this->memory) munmap(this->memory, this->length);
}

WlShmTarget* WlShmTarget::create(QSize size, uint32_t format) {
	static const auto* waylandIntegration = QtWaylandClient::QWaylandIntegration::instance();
	auto* shm = waylandIntegration->display()->shm();
	if (!shm || size.isEmpty()) return nullptr;

	const auto stride = static_cast<qsizetype>(size.width()) * 4;
	const auto length = static_cast<size_t>(stride) * static_cast<size_t>(size.height());
	if (length > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
		qCWarning(logShm) << "Shm buffer of size" << size << "is too large.";
		return nullptr;
	}

	auto fd = memfd_create("quickshell:shm", MFD_CLOEXEC);
	if (fd == -1) {
		qCWarning(logShm) << "Failed to create shm memory:" << qt_error_string(-1);
		return nullptr;
	}

	auto closeFd = qScopeGuard([&] { close(fd); });
	if (ftruncate(fd, static_cast<off_t>(length)) == -1) {
		qCWarning(logShm) << "Failed to size shm memory:" << qt_error_string(-1);
		return nullptr;
	}

	auto* memory = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (memory == MAP_FAILED) { // NOLINT
		qCWarning(logShm) << "Failed to map shm memory:" << qt_error_string(-1);
		return nullptr;
	}

	// Unmaps memory on failure.
	auto buffer = std::unique_ptr<WlShmTarget>(new WlShmTarget());
	buffer->memory = memory;
	buffer->length = length;
	buffer->format = format;
	buffer->mImage = QImage(
	    static_cast<uchar*>(memory),
	    size.width(),
	    size.height(),
	    stride,
	    QtWaylandClient::QWaylandShm::formatFrom(static_cast<::wl_shm_format>(format)) // NOLINT
	);

	// NOLINTNEXTLINE(misc-include-cleaner)
	auto* pool = wl_shm_create_pool(shm->object(), fd, static_cast<int32_t>(length));
	if (pool) {
		buffer->mBuffer = wl_shm_pool_create_buffer( // NOLINT(misc-include-cleaner)
		    pool,
		    0,
		    size.width(),
		    size.height(),
		    static_cast<int32_t>(stride),
		    format
		);
		wl_shm_pool_destroy(pool); // NOLINT(misc-include-cleaner)
	}

	if (!buffer->mBuffer) {
		qCWarning(logShm) << "Failed to create shm buffer of size" << size;
		return nullptr;
	}

	qCDebug(logShm) << "Created shm target of size" << size;
	return buffer.release();
}

bool WlShmTarget::isCompatible(const WlBufferRequest& request) const {
	if (QSize(static_cast<int>(request.width), static_cast<int>(request.height)) != this->size()) {
		return false;
	}

	return std::ranges::find(request.shm.formats, this->format) != request.shm.formats.end();
}

WlBufferQSGTexture* WlShmTarget::createQsgTexture(QQuickWindow* /*window*/) const {
	return nullptr;
}
} // namespace qs::wayland::buffer::shm
