#pragma once

#include <qfilesystemwatcher.h>
#include <qobject.h>
#include <qrunnable.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>

namespace qs::core {

class QmlDiskCache;

class QmlDiskCacheWriter: public QRunnable {
public:
	explicit QmlDiskCacheWriter(
	    QmlDiskCache* cache,
	    QString cachePath,
	    QString packPath,
	    QString configPath
	);

	void run() override;

private:
	QmlDiskCache* cache;
	QString cachePath;
	QString packPath;
	QString configPath;
};

class QmlDiskCache: public QObject {
	Q_OBJECT;

public:
	// The qml engine reads the cache path once, so this must run before it loads a document.
	// False where the cache would be slower than compiling.
	static bool enable();

	void restore(const QString& configPath);

	static QmlDiskCache* instance();

private slots:
	void save();

private:
	explicit QmlDiskCache();

	void onSaveCompleted();

	QString cachePath;
	QString packPath;
	QString configPath;
	QFileSystemWatcher watcher;
	QTimer saveTimer;
	bool restored = false;
	bool saveInProgress = false;
	bool saveQueued = false;

	friend class QmlDiskCacheWriter;
};

} // namespace qs::core
