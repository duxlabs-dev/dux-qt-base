#include "dbusimage.hpp"

#include <qdbusargument.h>
#include <qimage.h>
#include <qloggingcategory.h>
#include <qsize.h>
#include <qsysinfo.h>
#include <qtypes.h>

#include "../../core/logcat.hpp"

namespace qs::service::notifications {

// NOLINTNEXTLINE(misc-use-internal-linkage)
QS_DECLARE_LOGGING_CATEGORY(logNotifications); // server.cpp

QImage DBusNotificationImage::createImage() const {
	auto channels = this->hasAlpha ? 4 : 3;
	if (this->width <= 0 || this->height <= 0) return {};
	if (this->rowstride < qint64(this->width) * channels) return {};
	if (qint64(this->rowstride) * this->height > this->data.size()) return {};

	auto format = this->hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
	auto* pixels = new QByteArray(this->data);

	return QImage(
	    reinterpret_cast<const uchar*>(pixels->constData()),
	    this->width,
	    this->height,
	    this->rowstride,
	    format,
	    [](void* data) { delete static_cast<QByteArray*>(data); },
	    pixels
	);
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DBusNotificationImage& pixmap) {
	argument.beginStructure();
	argument >> pixmap.width;
	argument >> pixmap.height;
	argument >> pixmap.rowstride;
	argument >> pixmap.hasAlpha;
	auto sampleBits = qdbus_cast<qint32>(argument);
	auto channels = qdbus_cast<qint32>(argument);
	argument >> pixmap.data;
	argument.endStructure();

	if (sampleBits != 8 || channels != (pixmap.hasAlpha ? 4 : 3)
	    || pixmap.createImage().isNull()) {
		qCWarning(logNotifications) << "Invalid notification image data";
		pixmap = {};
	}

	return argument;
}

const QDBusArgument& operator<<(QDBusArgument& argument, const DBusNotificationImage& pixmap) {
	argument.beginStructure();
	argument << pixmap.width;
	argument << pixmap.height;
	argument << pixmap.rowstride;
	argument << pixmap.hasAlpha;
	argument << 8;
	argument << (pixmap.hasAlpha ? 4 : 3);
	argument << pixmap.data;
	argument.endStructure();
	return argument;
}

QImage
NotificationImage::requestImage(const QString& /*unused*/, QSize* size, const QSize& requestedSize) {
	auto image = this->image.createImage();

	if (size != nullptr) *size = image.size();
	auto targetSize = constrainedImageSize(image.size(), requestedSize);
	if (targetSize == image.size()) return image;
	return image.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace qs::service::notifications
