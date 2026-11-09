#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

class TestFloatingWindow: public QObject {
	Q_OBJECT;

private slots:
	void foreignParentHandle();
};
