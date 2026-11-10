#include "desktopentrymonitor.hpp"
#include <algorithm>

#include <qdir.h>
#include <qfileinfo.h>
#include <qfilesystemwatcher.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include "desktopentry.hpp"

namespace {
void addPathAndParents(QFileSystemWatcher& watcher, const QString& path) {
	watcher.addPath(path);

	auto p = QFileInfo(path).absolutePath();
	while (!p.isEmpty()) {
		watcher.addPath(p);
		const auto parent = QFileInfo(p).dir().absolutePath();
		if (parent == p) break;
		p = parent;
	}
}

bool isEntryDirectory(const QString& path) {
	return std::ranges::any_of(DesktopEntryManager::desktopPaths(), [&](const QString& entryPath) {
		return path == entryPath || path.startsWith(entryPath + '/');
	});
}
} // namespace

DesktopEntryMonitor::DesktopEntryMonitor(QObject* parent): QObject(parent) {
	this->debounceTimer.setSingleShot(true);
	this->debounceTimer.setInterval(100);

	QObject::connect(
	    &this->watcher,
	    &QFileSystemWatcher::directoryChanged,
	    this,
	    &DesktopEntryMonitor::onDirectoryChanged
	);
	QObject::connect(
	    &this->debounceTimer,
	    &QTimer::timeout,
	    this,
	    &DesktopEntryMonitor::processChanges
	);

	this->startMonitoring();
}

void DesktopEntryMonitor::startMonitoring() {
	for (const auto& path: DesktopEntryManager::desktopPaths()) {
		if (!QDir(path).exists()) continue;
		addPathAndParents(this->watcher, path);
		this->scanAndWatch(path);
	}
}

void DesktopEntryMonitor::scanAndWatch(const QString& dirPath) {
	auto dir = QDir(dirPath);
	if (!dir.exists()) return;

	this->watcher.addPath(dirPath);

	auto subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
	for (const auto& subdir: subdirs) this->watcher.addPath(subdir.absoluteFilePath());
}

// Ancestors are watched for an applications directory coming back, not for their other children.
void DesktopEntryMonitor::onDirectoryChanged(const QString& path) {
	if (!isEntryDirectory(path) && !this->watchRestoredPaths()) return;
	this->debounceTimer.start();
}

bool DesktopEntryMonitor::watchRestoredPaths() {
	const auto watched = this->watcher.directories();
	auto restored = false;

	for (const auto& path: DesktopEntryManager::desktopPaths()) {
		if (watched.contains(path) || !QDir(path).exists()) continue;
		this->scanAndWatch(path);
		restored = true;
	}

	return restored;
}

void DesktopEntryMonitor::processChanges() { emit this->desktopEntriesChanged(); }