#include "diskcache.hpp"
#include <algorithm>
#include <chrono>
#include <utility>

#include <qbytearray.h>
#include <qcontainerfwd.h>
#include <qdatastream.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qelapsedtimer.h>
#include <qfile.h>
#include <qfiledevice.h>
#include <qfileinfo.h>
#include <qfilesystemwatcher.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qoverload.h>
#include <qsavefile.h>
#include <qstorageinfo.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtenvironmentvariables.h>
#include <qthreadpool.h>
#include <qtimer.h>
#include <qtypes.h>

#include "logcat.hpp"
#include "paths.hpp"

namespace qs::core {

namespace {
QS_LOGGING_CATEGORY(logDiskCache, "quickshell.diskcache", QtWarningMsg);

constexpr quint32 PACK_MAGIC = 0x51435041;
constexpr quint32 PACK_VERSION = 1;
constexpr auto SAVE_DELAY = std::chrono::seconds(5);
constexpr auto RESTORE_BUDGET = std::chrono::milliseconds(500);
constexpr qint64 MAX_UNUSED_SECS = 30LL * 24 * 60 * 60;

const QStringList& entryFilters() {
	static const auto filters = QStringList {"*.qmlc", "*.jsc", "*.mjsc"};
	return filters;
}

QString runtimeCachePath() {
	auto* vfs = QsPaths::instance()->shellVfsDir();
	if (!vfs) return QString();
	return vfs->filePath("qmlcache");
}

bool writeEntry(const QString& path, const QByteArray& data, qint64 lastUsed) {
	auto file = QFile(path);
	const auto time = QDateTime::fromSecsSinceEpoch(lastUsed);

	// Unbuffered, or the write lands after the times are set.
	return file.open(QFile::WriteOnly | QFile::Unbuffered) && file.write(data) == data.size()
	    && file.setFileTime(time, QFile::FileModificationTime)
	    && file.setFileTime(time, QFile::FileAccessTime);
}

bool readHeader(QDataStream& stream, QString& configPath) {
	quint32 magic = 0;
	quint32 version = 0;
	stream.setVersion(QDataStream::Qt_6_0);
	stream >> magic >> version >> configPath;
	return stream.status() == QDataStream::Ok && magic == PACK_MAGIC && version == PACK_VERSION;
}

void unpack(const QString& packPath, const QString& cachePath) {
	auto pack = QFile(packPath);
	if (!pack.open(QFile::ReadOnly)) return;

	auto stream = QDataStream(&pack);
	QString configPath;

	if (!readHeader(stream, configPath)) {
		qCInfo(logDiskCache) << "Ignoring qml cache pack with an unknown format at" << packPath;
		return;
	}

	auto staging = QDir(cachePath % ".tmp");
	staging.removeRecursively();
	if (!staging.mkpath(".")) return;

	auto timer = QElapsedTimer();
	timer.start();
	auto restored = 0;

	// On a disk too slow to beat compiling, the engine compiles what is left.
	while (!stream.atEnd() && timer.durationElapsed() < RESTORE_BUDGET) {
		QString name;
		qint64 lastUsed = 0;
		QByteArray data;
		stream >> name >> lastUsed >> data;

		const auto valid = stream.status() == QDataStream::Ok && !name.isEmpty() && !name.contains('/');
		if (!valid || !writeEntry(staging.filePath(name), data, lastUsed)) {
			qCWarning(logDiskCache) << "Could not restore the qml cache from" << packPath;
			staging.removeRecursively();
			return;
		}

		restored++;
	}

	if (!QDir().rename(staging.path(), cachePath)) {
		staging.removeRecursively();
		return;
	}

	qCInfo(logDiskCache) << "Restored" << restored << "qml cache entries in" << timer.elapsed()
	                     << "ms, complete:" << stream.atEnd();
}

// Killed shells leave their mirror and cache in the runtime dir, which is memory.
void removeAbandonedCaches() {
	auto* paths = QsPaths::instance();
	const auto vfs = QDir(paths->baseRunDir()->filePath("vfs"));
	const auto own = paths->shellVfsDir()->dirName();

	for (const auto& shell: vfs.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (shell == own) continue;

		const auto shellVfs = QDir(vfs.filePath(shell));
		auto cache = QDir(shellVfs.filePath("qmlcache"));
		if (!cache.exists()) continue;

		const auto instances = paths->baseRunDir()->filePath("by-shell/" % shell);
		if (!std::get<0>(QsPaths::collectInstances(instances, QString())).isEmpty()) continue;

		qCDebug(logDiskCache) << "Removing abandoned qml cache of shell" << shell;
		cache.removeRecursively();
		if (!shellVfs.exists(".qmlls.ini")) QDir(shellVfs.filePath("qs")).removeRecursively();
	}
}

// An updated config under a new path, like a new embedded revision, starts a new pack.
void removeOrphanedPacks(const QString& packPath, const QString& configPath) {
	auto shells = QFileInfo(packPath).dir();
	const auto own = shells.dirName();
	if (!shells.cdUp()) return;

	const auto configs = QFileInfo(configPath).dir();

	for (const auto& shell: shells.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (shell == own) continue;

		auto pack = QFile(shells.filePath(shell % "/qmlcache.pack"));
		if (!pack.open(QFile::ReadOnly)) continue;

		auto stream = QDataStream(&pack);
		QString config;
		const auto valid = readHeader(stream, config);
		if (valid && (QFileInfo(config).dir() != configs || QFileInfo::exists(config))) continue;

		qCDebug(logDiskCache) << "Removing orphaned qml cache pack" << pack.fileName();
		pack.remove();
		shells.rmdir(shell);
	}
}

bool hasUnsavedEntries(const QString& cachePath, const QString& packPath) {
	const auto saved = QFileInfo(packPath).lastModified();
	const auto entries = QDir(cachePath).entryInfoList(entryFilters(), QDir::Files);
	if (!saved.isValid()) return !entries.isEmpty();

	return std::ranges::any_of(entries, [&](const QFileInfo& entry) {
		return entry.lastModified() > saved;
	});
}
} // namespace

QmlDiskCacheWriter::QmlDiskCacheWriter(
    QmlDiskCache* cache,
    QString cachePath,
    QString packPath,
    QString configPath
)
    : cache(cache)
    , cachePath(std::move(cachePath))
    , packPath(std::move(packPath))
    , configPath(std::move(configPath)) {
	this->setAutoDelete(true);
}

void QmlDiskCacheWriter::run() {
	auto pack = QSaveFile(this->packPath);
	auto stream = QDataStream(&pack);
	stream.setVersion(QDataStream::Qt_6_0);

	if (pack.open(QFile::WriteOnly)) {
		stream << PACK_MAGIC << PACK_VERSION << this->configPath;
		const auto oldest = QDateTime::currentSecsSinceEpoch() - MAX_UNUSED_SECS;

		for (const auto& entry: QDir(this->cachePath).entryInfoList(entryFilters(), QDir::Files)) {
			// The engine maps entries, which only moves the access time.
			const auto lastUsed = std::max(entry.lastModified(), entry.lastRead()).toSecsSinceEpoch();
			if (lastUsed < oldest) continue;

			auto file = QFile(entry.filePath());
			if (!file.open(QFile::ReadOnly)) continue;
			stream << entry.fileName() << lastUsed << file.readAll();
		}
	}

	if (stream.status() != QDataStream::Ok || !pack.commit()) {
		qCWarning(logDiskCache) << "Could not save the qml cache to" << this->packPath;
	}

	removeOrphanedPacks(this->packPath, this->configPath);

	QMetaObject::invokeMethod(
	    this->cache,
	    [cache = this->cache] { cache->onSaveCompleted(); },
	    Qt::QueuedConnection
	);
}

QmlDiskCache::QmlDiskCache() {
	this->saveTimer.setSingleShot(true);
	this->saveTimer.setInterval(SAVE_DELAY);

	// clang-format off
	QObject::connect(&this->saveTimer, &QTimer::timeout, this, &QmlDiskCache::save);
	QObject::connect(&this->watcher, &QFileSystemWatcher::directoryChanged, &this->saveTimer, qOverload<>(&QTimer::start));
	// clang-format on
}

QmlDiskCache* QmlDiskCache::instance() {
	static auto* instance = new QmlDiskCache(); // NOLINT
	return instance;
}

bool QmlDiskCache::enable() {
	if (qEnvironmentVariableIsSet("QML_DISABLE_DISK_CACHE")) return false;
	if (qEnvironmentVariableIsSet("QML_DISK_CACHE_PATH")) return true;

	const auto path = runtimeCachePath();
	if (path.isEmpty()) return false;

	// The engine syncs every entry it writes, which is only free in memory.
	const auto filesystem = QStorageInfo(QFileInfo(path).path()).fileSystemType();
	if (filesystem != "tmpfs" && filesystem != "ramfs") {
		qCInfo(logDiskCache) << "QML disk cache disabled, the runtime dir is on" << filesystem;
		return false;
	}

	qputenv("QML_DISK_CACHE_PATH", path.toLocal8Bit());
	return true;
}

void QmlDiskCache::restore(const QString& configPath) {
	if (this->restored) return;
	this->restored = true;

	const auto path = runtimeCachePath();
	if (path.isEmpty() || qEnvironmentVariable("QML_DISK_CACHE_PATH") != path) return;

	this->cachePath = path;
	this->configPath = configPath;
	this->packPath = QsPaths::instance()->shellCacheDir().filePath("qmlcache.pack");
	const auto cacheDir = QDir(this->cachePath);

	removeAbandonedCaches();

	if (!cacheDir.exists()) {
		unpack(this->packPath, this->cachePath);
	} else if (hasUnsavedEntries(this->cachePath, this->packPath)) {
		this->saveTimer.start();
	}

	if (!cacheDir.mkpath(".")) {
		qCCritical(logDiskCache) << "Could not create the qml cache dir at" << this->cachePath;
		return;
	}

	this->watcher.addPath(this->cachePath);
}

void QmlDiskCache::save() {
	if (this->saveInProgress) {
		this->saveQueued = true;
		return;
	}

	this->saveInProgress = true;
	this->saveQueued = false;
	auto* writer = new QmlDiskCacheWriter(this, this->cachePath, this->packPath, this->configPath);
	QThreadPool::globalInstance()->start(writer);
}

void QmlDiskCache::onSaveCompleted() {
	this->saveInProgress = false;
	if (this->saveQueued) this->save();
}

} // namespace qs::core
