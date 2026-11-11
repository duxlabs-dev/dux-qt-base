#pragma once

#include <qbytearray.h>
#include <qdir.h>
#include <qlist.h>
#include <qstring.h>

#include "scan.hpp"

namespace qs::core {

class QmlToolingSupport {
public:
	static bool updateTooling(const QDir& configRoot, bool keepMirror);
	static QString
	updateMirror(const QDir& configRoot, const QmlScanner& scanner, bool copyDocuments);

private:
	struct Document {
		QString source;
		QString target;
		bool fresh;
	};

	static QString getQmllsConfig();
	static bool lockTooling();
	static bool updateQmllsConfig(const QDir& configRoot, bool create);
	static bool mirrorDir(
	    const QmlScanner& scanner,
	    const QDir& source,
	    const QDir& target,
	    QList<Document>* documents
	);
	static bool mirrorEntry(
	    const QmlScanner& scanner,
	    const QString& path,
	    const QString& target,
	    QList<Document>* documents,
	    bool fresh
	);
	static bool copyDocuments(const QList<Document>& documents);
	static bool copyDocument(const Document& document);
	static bool writeMirrorFile(const QString& path, const QByteArray& data, bool fresh);
	static bool linkMirrorEntry(const QString& path, const QString& target, bool fresh);
	static void removeMirrorEntry(const QString& path);
	static inline bool toolingEnabled = false;
	static inline QFile* toolingLock = nullptr;
};

} // namespace qs::core
