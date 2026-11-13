#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

class TestFileView: public QObject {
	Q_OBJECT;

private slots:
	static void failedLoadStartsAnotherLoad_data(); // NOLINT(readability-identifier-naming)
	static void failedLoadStartsAnotherLoad();
	static void savedStartsAnotherWrite_data(); // NOLINT(readability-identifier-naming)
	static void savedStartsAnotherWrite();
	static void dataChangedWaitsForReplacement();
	static void completionCanDestroyView();
	static void dataChangeCanDestroyView();
	static void pathChangeCanDestroyView();
	static void readCanDestroyView_data(); // NOLINT(readability-identifier-naming)
	static void readCanDestroyView();
	static void writeCanDestroyView();
	static void waitedWriteKeepsCallbackWrite();
};
