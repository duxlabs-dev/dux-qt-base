#include "dbusimage.hpp"

#include <qbytearray.h>
#include <qdbusargument.h>
#include <qimage.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qsize.h>
#include <qsysinfo.h>
#include <qtypes.h>

#include "../../core/imageprovider.hpp"
#include "../../core/logcat.hpp"

namespace qs::service::notifications {

// NOLINTNEXTLINE(misc-use-internal-linkage)
QS_DECLARE_LOGGING_CATEGORY(logNotifications); // server.cpp

namespace {

// GdkPixbuf sends the last row without its rowstride padding, but QImage reads whole rows.
bool padLastRow(DBusNotificationImage& pixmap) {
	if (pixmap.width <= 0 || pixmap.height <= 0) return false;

	auto rowBytes = static_cast<qint64>(pixmap.width) * (pixmap.hasAlpha ? 4 : 3);
	auto padding = pixmap.rowstride - rowBytes;
	if (padding < 0) return false;

	auto size = static_cast<qint64>(pixmap.rowstride) * pixmap.height;
	auto missing = size - pixmap.data.size();
	if (missing <= 0) return true;
	// More than the padding is a truncated image; the size bound stops a bogus rowstride allocating.
	if (missing > padding || missing > pixmap.data.size()) return false;

	pixmap.data.resize(size, '\0');
	return true;
}

} // namespace

QImage DBusNotificationImage::createImage() const {
	auto channels = this->hasAlpha ? 4 : 3;
	if (this->width <= 0 || this->height <= 0) return {};
	if (this->rowstride < static_cast<qint64>(this->width) * channels) return {};
	if (static_cast<qint64>(this->rowstride) * this->height > this->data.size()) return {};

	auto format = this->hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
	auto* pixels = new QByteArray(this->data);

	auto image = QImage(
	    reinterpret_cast<const uchar*>(pixels->constData()),
	    this->width,
	    this->height,
	    this->rowstride,
	    format,
	    [](void* data) { delete static_cast<QByteArray*>(data); },
	    pixels
	);

	// A null image never took the cleanup function.
	if (image.isNull()) delete pixels;
	return image;
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

	if (sampleBits != 8 || channels != (pixmap.hasAlpha ? 4 : 3) || !padLastRow(pixmap)
	    || pixmap.createImage().isNull())
	{
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

QImage NotificationImage::requestImage(
    const QString& /*unused*/,
    QSize* size,
    const QSize& requestedSize
) {
	auto image = this->image.createImage();

	if (size != nullptr) *size = image.size();
	auto targetSize = constrainedImageSize(image.size(), requestedSize);
	if (targetSize == image.size()) return image;
	return image.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace qs::service::notifications
