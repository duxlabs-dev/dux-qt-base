#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qstringlist.h>
#include <qvariant.h>

class StringMap: public QObject {
	Q_OBJECT;

public:
	Q_INVOKABLE [[nodiscard]] QVariant get(const QString& key) const {
		return this->values.value(key);
	}
	Q_INVOKABLE [[nodiscard]] bool has(const QString& key) const {
		return this->values.contains(key);
	}
	Q_INVOKABLE void set(const QString& key, const QVariant& value) {
		this->values.insert(key, value);
	}
	Q_INVOKABLE [[nodiscard]] QStringList keys() const { return this->values.keys(); }
	Q_INVOKABLE void clear() { this->values.clear(); }

private:
	QHash<QString, QVariant> values;
};
