#include "icontheme.hpp"
#include <algorithm>
#include <chrono>
#include <utility>

#include <qchar.h>
#include <qcontainerfwd.h>
#include <qdir.h>
#include <qdiriterator.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qicon.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qstandardpaths.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qstringview.h>
#include <qtenvironmentvariables.h>
#include <qthreadpool.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "desktopentry.hpp"
#include "logcat.hpp"

namespace {
QS_LOGGING_CATEGORY(logIconTheme, "quickshell.icontheme", QtWarningMsg);

constexpr qint64 APPLICATION_SCORE = 3000000000;
constexpr qint64 CATEGORY_SCORE = 1000000000;
constexpr qint64 CONTEXT_SCORE = 100000000;
constexpr qint64 CHAIN_SCORE = 1000000;
constexpr qint64 SVG_SCORE = 100000;
constexpr qint64 SCALABLE_SCORE = 1000;
constexpr qint64 LARGEST_PREFERRED_RASTER = 256;
constexpr auto RESCAN_DELAY = std::chrono::seconds(2);

struct ScanDirs {
	// Theme directories, the theme's own before its parents'.
	QStringList search;
	// Directories that hold icons outside of any theme.
	QStringList loose;
};

struct ScanResult {
	QHash<QString, QString> files;
	QHash<QString, qint64> scores;
};

QStringList inheritedThemes(const QString& indexPath) {
	auto file = QFile(indexPath);
	if (!file.open(QFile::ReadOnly)) return {};

	while (!file.atEnd()) {
		const auto line = QString::fromUtf8(file.readLine());
		if (!line.startsWith("Inherits=")) continue;
		return line.sliced(9).trimmed().remove('"').replace(',', ' ').split(' ', Qt::SkipEmptyParts);
	}

	return {};
}

QStringList searchDirs(const QString& theme, const QStringList& baseDirs) {
	auto themes = QStringList();
	auto queue = QStringList(theme);

	while (!queue.isEmpty()) {
		const auto current = queue.takeFirst();
		if (current.isEmpty() || themes.contains(current)) continue;
		themes.append(current);

		for (const auto& base: baseDirs) {
			const auto indexPath = base + '/' + current + "/index.theme";
			if (!QFileInfo(indexPath).isFile()) continue;
			queue.append(inheritedThemes(indexPath));
			break;
		}
	}

	if (!themes.contains("hicolor")) themes.append("hicolor");

	auto dirs = QStringList();
	for (const auto& name: std::as_const(themes)) {
		for (const auto& base: baseDirs) {
			const auto dir = base + '/' + name;
			if (QFileInfo(dir).isDir()) dirs.append(dir);
		}
	}

	return dirs;
}

ScanDirs scanDirs(const QString& theme) {
	const auto dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	auto dataDirs = QStringList(dataHome);

	auto systemDirs = qEnvironmentVariable("XDG_DATA_DIRS");
	if (systemDirs.trimmed().isEmpty()) systemDirs = "/usr/local/share:/usr/share";
	dataDirs.append(systemDirs.split(':', Qt::SkipEmptyParts));
	dataDirs.append(dataHome + "/flatpak/exports/share");
	dataDirs.append("/var/lib/flatpak/exports/share");

	auto baseDirs = QStringList({QDir::homePath() + "/.icons", dataHome + "/icons"});
	auto looseDirs = baseDirs;

	for (const auto& dir: std::as_const(dataDirs)) {
		baseDirs.append(dir + "/icons");
		looseDirs.append(dir + "/pixmaps");
	}

	baseDirs.removeDuplicates();
	looseDirs.removeDuplicates();
	return {.search = searchDirs(theme, baseDirs), .loose = looseDirs};
}

qsizetype skipDigits(QStringView text, qsizetype from) {
	auto i = from;
	while (i < text.size() && text.at(i) >= u'0' && text.at(i) <= u'9') i++;
	return i;
}

// The size named by a "48", "48x48" or "48x48@2x" path segment, or -1.
qint64 segmentSize(QStringView segment) {
	auto end = skipDigits(segment, 0);
	if (end == 0) return -1;
	const auto size = segment.first(end).toLongLong();

	if (end < segment.size() && segment.at(end) == u'x') {
		const auto next = skipDigits(segment, end + 1);
		if (next == end + 1) return -1;
		end = next;
	}

	if (end < segment.size() && segment.at(end) == u'@') {
		const auto next = skipDigits(segment, end + 1);
		if (next == end + 1 || next >= segment.size() || segment.at(next) != u'x') return -1;
		end = next + 1;
	}

	return end == segment.size() ? size : -1;
}

qint64 rasterScore(qint64 size) {
	if (size <= LARGEST_PREFERRED_RASTER) return LARGEST_PREFERRED_RASTER + size;
	return std::max<qint64>(0, LARGEST_PREFERRED_RASTER - size / 8);
}

// The position of the scan directory that holds a directory, theme directories first.
qsizetype chainIndex(const QString& directory, const ScanDirs& dirs) {
	const auto holds = [&](const QString& dir) { return directory.startsWith(dir + '/'); };

	const auto search = std::ranges::find_if(dirs.search, holds);
	if (search != dirs.search.end()) return search - dirs.search.begin();

	const auto loose = std::ranges::find_if(dirs.loose, holds);
	if (loose != dirs.loose.end()) return dirs.search.size() + (loose - dirs.loose.begin());

	return dirs.search.size() + dirs.loose.size();
}

// Application icons win over other contexts, then earlier themes, then scalable and large sizes.
qint64 directoryScore(const QString& directory, const ScanDirs& dirs) {
	const auto count = dirs.search.size() + dirs.loose.size();
	auto score = std::clamp<qint64>(count - chainIndex(directory, dirs), 0, 99) * CHAIN_SCORE;

	static const auto contexts = QStringList({
	    "/places/",
	    "/devices/",
	    "/mimetypes/",
	    "/status/",
	    "/actions/",
	});

	const auto inDirectory = [&](const QString& context) { return directory.contains(context); };
	if (directory.contains("/apps/")) score += APPLICATION_SCORE;
	else if (directory.contains("/categories/")) score += CATEGORY_SCORE;
	else if (std::ranges::any_of(contexts, inDirectory)) score += CONTEXT_SCORE;

	if (directory.contains("/scalable/")) return score + SCALABLE_SCORE;

	const auto segments = QStringView(directory).split(u'/', Qt::SkipEmptyParts);
	for (const auto segment: segments) {
		const auto size = segmentSize(segment);
		if (size >= 0) return score + rasterScore(size);
	}

	return score;
}

bool isNameChar(QChar c) {
	const auto code = c.unicode();
	if (code >= u'a' && code <= u'z') return true;
	if (code >= u'A' && code <= u'Z') return true;
	if (code >= u'0' && code <= u'9') return true;
	return code == u'_' || code == u'.' || code == u'+' || code == u'-';
}

void addFile(ScanResult& result, const QString& path, qint64 directoryScore) {
	const auto separator = path.lastIndexOf('/') + 1;
	const auto extension = path.lastIndexOf('.');
	if (extension <= separator) return;

	const auto name = QStringView(path).sliced(separator, extension - separator);
	if (!std::ranges::all_of(name, isNameChar)) return;

	const auto key = name.toString();
	const auto score = directoryScore + (path.endsWith(".svg") ? SVG_SCORE : 0);
	if (score <= result.scores.value(key, -1)) return;

	result.scores.insert(key, score);
	result.files.insert(key, path);
}

// Walks directories the way find -L does: links are followed, so a directory is scanned under
// every path that reaches it, and one that contains itself is skipped.
struct Scan {
	explicit Scan(const ScanDirs& dirs): dirs(dirs) {}

	const ScanDirs& dirs;
	ScanResult result;
	QStringList ancestors;

	void directory(const QString& path, const QStringList& suffixes, bool recursive) {
		const auto canonical = QFileInfo(path).canonicalFilePath();
		if (canonical.isEmpty() || this->ancestors.contains(canonical)) return;
		this->ancestors.append(canonical);

		const auto score = directoryScore(path + '/', this->dirs);
		const auto filters = QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot;
		auto iter = QDirIterator(path, filters);

		while (iter.hasNext()) {
			const auto entry = iter.next();
			const auto info = iter.fileInfo();

			if (info.isDir()) {
				if (recursive) this->directory(entry, suffixes, recursive);
			} else if (suffixes.contains(info.suffix())) {
				addFile(this->result, entry, score);
			}
		}

		this->ancestors.removeLast();
	}
};
} // namespace

IconThemeScanner::IconThemeScanner(IconThemeIndex* index, QString theme)
    : index(index)
    , theme(std::move(theme)) {
	this->setAutoDelete(true);
}

void IconThemeScanner::run() {
	const auto dirs = scanDirs(this->theme);
	auto scan = Scan(dirs);

	for (const auto& dir: dirs.search) scan.directory(dir, {"svg", "png"}, true);
	for (const auto& dir: dirs.loose) scan.directory(dir, {"svg", "png", "xpm"}, false);

	QMetaObject::invokeMethod(
	    this->index,
	    [index = this->index, theme = this->theme, files = std::move(scan.result.files)]() mutable {
		    index->onScanCompleted(theme, std::move(files));
	    },
	    Qt::QueuedConnection
	);
}

IconThemeIndex::IconThemeIndex() {
	this->rescanTimer.setSingleShot(true);
	this->rescanTimer.setInterval(RESCAN_DELAY);
	QObject::connect(&this->rescanTimer, &QTimer::timeout, this, &IconThemeIndex::scan);
}

IconThemeIndex* IconThemeIndex::instance() {
	static auto* instance = new IconThemeIndex(); // NOLINT
	return instance;
}

void IconThemeIndex::setTheme(const QString& theme) {
	const auto previous = QIcon::themeName();
	QIcon::setThemeName(theme);
	const auto current = QIcon::themeName();

	if (current != previous) emit this->themeChanged();
	this->index(current);
}

QString IconThemeIndex::file(const QString& icon) {
	if (!this->started) this->index(QIcon::themeName());
	return this->files.value(icon);
}

void IconThemeIndex::index(const QString& theme) {
	if (this->started && theme == this->indexedTheme) return;
	this->indexedTheme = theme;

	if (!this->started) {
		this->started = true;

		QObject::connect(
		    DesktopEntryManager::instance(),
		    &DesktopEntryManager::applicationsChanged,
		    this,
		    &IconThemeIndex::onApplicationsChanged
		);
	}

	this->scan();
}

// Installing an application usually installs its icons. The first index is still scanning
// when the desktop entries first load.
void IconThemeIndex::onApplicationsChanged() {
	if (this->mRevision != 0) this->rescanTimer.start();
}

void IconThemeIndex::scan() {
	if (this->scanInProgress) {
		this->scanQueued = true;
		return;
	}

	qCDebug(logIconTheme) << "Scanning icon theme" << this->indexedTheme;
	this->scanInProgress = true;
	this->scanQueued = false;
	auto* scanner = new IconThemeScanner(this, this->indexedTheme);
	QThreadPool::globalInstance()->start(scanner);
}

void IconThemeIndex::onScanCompleted(const QString& theme, QHash<QString, QString> files) {
	this->scanInProgress = false;

	// A scan of a theme that was replaced meanwhile left scanQueued set.
	const auto current = theme == this->indexedTheme;
	if (current && (this->mRevision == 0 || files != this->files)) {
		qCDebug(logIconTheme) << "Indexed" << files.size() << "icons of" << theme;
		this->files = std::move(files);
		this->mRevision++;
		emit this->revisionChanged();
	}

	if (this->scanQueued) this->scan();
}
