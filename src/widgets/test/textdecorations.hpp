#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

class TestTextDecorations: public QObject {
	Q_OBJECT;

private slots:
	static void deltasRebuildText();
	static void deltasAreMinimal();
	static void formatsLeaveTextAlone();
	static void textKeepsRawCharacters();
};
