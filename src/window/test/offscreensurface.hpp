#pragma once

#include <qobject.h>

class TestOffscreenSurfaceAffinity: public QObject {
	Q_OBJECT

private slots:
	void surfaceTakesCreatingThreadAffinity();
	void guiThreadSurfaceIsGuiAffinedAndUsable();
};
