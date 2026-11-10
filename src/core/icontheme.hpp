#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qrunnable.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

class IconThemeIndex;

class IconThemeScanner: public QRunnable {
public:
	explicit IconThemeScanner(IconThemeIndex* index, QString theme);

	void run() override;

private:
	IconThemeIndex* index;
	QString theme;
};

// The best file for each icon name of the icon theme, its parents, hicolor and the loose icon
// directories. Scanned on the thread pool, so lookups never touch the filesystem.
class IconThemeIndex: public QObject {
	Q_OBJECT;

public:
	// Sets Qt's icon theme as well. An empty name returns to the system theme.
	void setTheme(const QString& theme);

	// Indexes the current theme on first use and returns nothing until that scan is done.
	[[nodiscard]] QString file(const QString& icon);
	[[nodiscard]] qint32 revision() const { return this->mRevision; }

	static IconThemeIndex* instance();

signals:
	void themeChanged();
	void revisionChanged();

private slots:
	void onApplicationsChanged();
	void scan();

private:
	explicit IconThemeIndex();

	void index(const QString& theme);
	void onScanCompleted(const QString& theme, QHash<QString, QString> files);

	QString indexedTheme;
	QHash<QString, QString> files;
	qint32 mRevision = 0;
	QTimer rescanTimer;
	bool started = false;
	bool scanInProgress = false;
	bool scanQueued = false;

	friend class IconThemeScanner;
};
