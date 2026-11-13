#include "fileview.hpp"
#include <memory>

#include <qfile.h>
#include <qobject.h>
#include <qsignalspy.h>
#include <qtemporarydir.h>
#include <qtest.h>
#include <qtestcase.h>

#include "../fileview.hpp"

using qs::io::FileView;
using qs::io::FileViewError;

void TestFileView::failedLoadStartsAnotherLoad_data() {
	QTest::addColumn<bool>("wait");
	QTest::newRow("async") << false;
	QTest::newRow("wait") << true;
}

void TestFileView::failedLoadStartsAnotherLoad() {
	QFETCH(const bool, wait);
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto file = QFile(dir.filePath("present"));
	QVERIFY(file.open(QFile::WriteOnly));
	QCOMPARE(file.write("loaded"), 6);
	file.close();

	auto view = FileView();
	view.bindablePrintErrors().setValue(false);
	auto failures = QSignalSpy(&view, &FileView::loadFailed);
	auto loads = QSignalSpy(&view, &FileView::loaded);
	auto attempts = 0;
	QObject::connect(&view, &FileView::loadFailed, &view, [&](FileViewError::Enum error) {
		QCOMPARE(error, FileViewError::FileNotFound);
		view.setPath(dir.filePath(++attempts == 1 ? "also-missing" : "present"));
	});

	view.setPath(dir.filePath("missing"));
	if (wait) QVERIFY(view.waitForJob());

	QTRY_COMPARE(failures.count(), 2);
	QTRY_COMPARE(loads.count(), 1);
	QCOMPARE(view.text(), QStringLiteral("loaded"));
	QCOMPARE(view.path(), file.fileName());
	QVERIFY(!view.waitForJob());
}

void TestFileView::savedStartsAnotherWrite_data() {
	QTest::addColumn<bool>("wait");
	QTest::newRow("async") << false;
	QTest::newRow("wait") << true;
}

void TestFileView::savedStartsAnotherWrite() {
	QFETCH(const bool, wait);
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto view = FileView();
	view.bindablePrintErrors().setValue(false);
	view.setPath(dir.filePath("written"));
	QVERIFY(view.waitForJob());
	auto saves = QSignalSpy(&view, &FileView::saved);
	auto writes = 0;
	QObject::connect(&view, &FileView::saved, &view, [&]() {
		if (++writes == 1) view.setText(QStringLiteral("second"));
	});

	view.setText(QStringLiteral("first"));
	if (wait) QVERIFY(view.waitForJob());

	QTRY_COMPARE(saves.count(), 2);
	auto file = QFile(view.path());
	QVERIFY(file.open(QFile::ReadOnly));
	QCOMPARE(file.readAll(), QByteArray("second"));
	QCOMPARE(view.text(), QStringLiteral("second"));
	QVERIFY(!view.waitForJob());
}

void TestFileView::dataChangedWaitsForReplacement() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto file = QFile(dir.filePath("present"));
	QVERIFY(file.open(QFile::WriteOnly));
	QCOMPARE(file.write("loaded"), 6);
	file.close();
	auto view = FileView();
	view.bindablePrintErrors().setValue(false);
	auto failures = QSignalSpy(&view, &FileView::loadFailed);
	auto loads = QSignalSpy(&view, &FileView::loaded);
	auto changed = false;
	QObject::connect(&view, &FileView::dataChanged, &view, [&]() {
		if (changed) return;
		changed = true;
		QVERIFY(!view.waitForJob());
		view.setPath(file.fileName());
		QVERIFY(view.waitForJob());
	});

	view.setPath(dir.filePath("missing"));
	QTRY_COMPARE(loads.count(), 1);
	QCOMPARE(failures.count(), 0);
	QCOMPARE(view.path(), file.fileName());
	QCOMPARE(view.text(), QStringLiteral("loaded"));
}

void TestFileView::completionCanDestroyView() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto view = std::make_unique<FileView>();
	view->bindablePrintErrors().setValue(false);
	QObject::connect(view.get(), &FileView::loadFailed, view.get(), [&]() { view.reset(); });
	view->setPath(dir.filePath("missing"));
	QTRY_VERIFY(!view);
}

void TestFileView::dataChangeCanDestroyView() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto view = std::make_unique<FileView>();
	view->bindablePrintErrors().setValue(false);
	QVERIFY(view->setProperty("__preload", false));
	QObject::connect(view.get(), &FileView::dataChanged, view.get(), [&]() { view.reset(); });
	view->setPath(dir.filePath("missing"));
	QTRY_VERIFY(!view);
}

void TestFileView::pathChangeCanDestroyView() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto view = std::make_unique<FileView>();
	view->bindablePrintErrors().setValue(false);
	QObject::connect(view.get(), &FileView::pathChanged, view.get(), [&]() { view.reset(); });
	view->setPath(dir.filePath("missing"));
	QTRY_VERIFY(!view);
}

void TestFileView::readCanDestroyView_data() {
	QTest::addColumn<bool>("text");
	QTest::newRow("text") << true;
	QTest::newRow("data") << false;
}

void TestFileView::readCanDestroyView() {
	QFETCH(const bool, text);
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto view = std::make_unique<FileView>();
	view->bindablePrintErrors().setValue(false);
	QVERIFY(view->setProperty("__preload", false));
	QVERIFY(view->setProperty("__blockAllReads", true));
	QObject::connect(view.get(), &FileView::loadFailed, view.get(), [&]() { view.reset(); });
	view->setPath(dir.filePath("missing"));
	if (text) view->text();
	else view->data();
	QVERIFY(!view);
}

void TestFileView::writeCanDestroyView() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto view = std::make_unique<FileView>();
	view->bindablePrintErrors().setValue(false);
	view->setPath(dir.filePath("written"));
	QVERIFY(view->waitForJob());
	QObject::connect(view.get(), &FileView::saved, view.get(), [&]() { view.reset(); });
	view->setText(QStringLiteral("first"));
	view->setText(QStringLiteral("second"));
	QVERIFY(!view);
}

void TestFileView::waitedWriteKeepsCallbackWrite() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto view = FileView();
	view.bindablePrintErrors().setValue(false);
	view.setPath(dir.filePath("written"));
	QVERIFY(view.waitForJob());
	auto saves = QSignalSpy(&view, &FileView::saved);
	auto writes = 0;
	QObject::connect(&view, &FileView::saved, &view, [&]() {
		auto file = QFile(view.path());
		QVERIFY(file.open(QFile::ReadOnly));
		switch (++writes) {
		case 1:
			QCOMPARE(file.readAll(), QByteArray("first"));
			view.setText(QStringLiteral("nested"));
			break;
		case 2: QCOMPARE(file.readAll(), QByteArray("nested")); break;
		case 3: QCOMPARE(file.readAll(), QByteArray("second")); break;
		default: QFAIL("Unexpected write completion");
		}
	});
	view.setText(QStringLiteral("first"));
	view.setText(QStringLiteral("second"));
	QTRY_COMPARE(saves.count(), 3);
	auto file = QFile(view.path());
	QVERIFY(file.open(QFile::ReadOnly));
	QCOMPARE(file.readAll(), QByteArray("second"));
	QVERIFY(!view.waitForJob());
}

QTEST_GUILESS_MAIN(TestFileView);
