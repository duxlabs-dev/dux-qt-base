#include "textdecorations.hpp"
#include <memory>

#include <qlist.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qquickitem.h>
#include <qquicktextdocument.h>
#include <qsignalspy.h>
#include <qstring.h>
#include <qtest.h>
#include <qtestcase.h>
#include <qtextcursor.h>
#include <qtextdocument.h>
#include <qtextformat.h>
#include <qtextlayout.h>
#include <qtextobject.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../textdecorations.hpp"

using qs::widgets::TextDecorations;

namespace {

struct Editor {
	Editor() {
		auto component = QQmlComponent(&this->engine);
		component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.PlainText }", QUrl());
		this->item.reset(qobject_cast<QQuickItem*>(component.create()));
		this->document = this->item->property("textDocument").value<QQuickTextDocument*>();
		this->decorations.setDocument(this->document);
	}

	[[nodiscard]] QTextDocument* text() const { return this->document->textDocument(); }

	QQmlEngine engine;
	std::unique_ptr<QQuickItem> item;
	QQuickTextDocument* document = nullptr;
	TextDecorations decorations;
};

} // namespace

void TestTextDecorations::deltasRebuildText() {
	auto editor = Editor();
	auto* document = editor.text();
	auto mirror = QString();

	QObject::connect(
	    &editor.decorations,
	    &TextDecorations::contentsChanged,
	    &editor.decorations,
	    [&](qint32 position, qint32 removed, qint32 added) {
		    QVERIFY(position + removed <= mirror.size());
		    mirror.replace(position, removed, editor.decorations.text(position, position + added));
	    }
	);

	auto cursor = QTextCursor(document);
	cursor.insertText(QStringLiteral("first\nsecond"));
	cursor.movePosition(QTextCursor::End);
	cursor.insertText(QStringLiteral("\nthird"));
	cursor.setPosition(3);
	cursor.setPosition(9, QTextCursor::KeepAnchor);
	cursor.insertText(QStringLiteral("X"));
	cursor.movePosition(QTextCursor::End);
	cursor.deletePreviousChar();
	document->undo();
	document->undo();
	document->redo();
	cursor.select(QTextCursor::Document);
	cursor.insertText(QStringLiteral("all\nnew"));
	document->setPlainText(QStringLiteral("reset\ntext"));

	QCOMPARE(mirror, document->toPlainText());
}

void TestTextDecorations::deltasAreMinimal() {
	auto editor = Editor();
	auto cursor = QTextCursor(editor.text());
	cursor.insertText(QStringLiteral("ab\ncd"));

	auto spy = QSignalSpy(&editor.decorations, &TextDecorations::contentsChanged);
	cursor.movePosition(QTextCursor::End);
	cursor.insertText(QStringLiteral("e"));
	cursor.deletePreviousChar();
	cursor.setPosition(1);
	cursor.insertText(QStringLiteral("\n"));

	QCOMPARE(spy.count(), 3);
	QCOMPARE(spy.at(0), (QVariantList {5, 0, 1}));
	QCOMPARE(spy.at(1), (QVariantList {5, 1, 0}));
	QCOMPARE(spy.at(2), (QVariantList {1, 0, 1}));
}

void TestTextDecorations::formatsLeaveTextAlone() {
	auto editor = Editor();
	auto* document = editor.text();
	auto cursor = QTextCursor(document);
	cursor.insertText(QStringLiteral("keyword rest\nline"));

	auto entry = QVariantMap();
	entry.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
	entry.insert(QStringLiteral("bold"), true);
	editor.decorations.setPalette({QVariantMap(), entry});
	editor.decorations.setSpellColor(Qt::blue);

	auto spy = QSignalSpy(&editor.decorations, &TextDecorations::contentsChanged);
	auto undoSteps = document->availableUndoSteps();

	QVERIFY(editor.decorations.setLine(0, {0, 7, 1, 7, 5, 0}, {8, 100}, {}));
	QVERIFY(!editor.decorations.setLine(0, {0, 7, 1, 7, 5, 0}, {8, 100}, {}));
	QVERIFY(!editor.decorations.setLine(5, {0, 1, 1}, {}, {}));

	auto formats = document->firstBlock().layout()->formats();
	QCOMPARE(formats.size(), 2);
	QCOMPARE(formats.at(0).start, 0);
	QCOMPARE(formats.at(0).length, 7);
	QCOMPARE(formats.at(0).format.foreground().color(), QColor(Qt::red));
	QCOMPARE(formats.at(0).format.fontWeight(), QFont::Bold);
	QCOMPARE(formats.at(1).start, 8);
	QCOMPARE(formats.at(1).length, 4);
	QCOMPARE(formats.at(1).format.underlineStyle(), QTextCharFormat::SingleUnderline);

	editor.decorations.clear();
	QVERIFY(document->firstBlock().layout()->formats().isEmpty());

	QCOMPARE(spy.count(), 0);
	QCOMPARE(document->availableUndoSteps(), undoSteps);
}

void TestTextDecorations::textKeepsRawCharacters() {
	auto editor = Editor();
	auto cursor = QTextCursor(editor.text());
	cursor.insertText(QStringLiteral("a b c\nd"));

	QCOMPARE(editor.decorations.text(0, 7), QStringLiteral("a b c\nd"));
	QCOMPARE(editor.decorations.text(2, 100), QStringLiteral("b c\nd"));
	QCOMPARE(editor.decorations.text(5, 2), QString());
}

void TestTextDecorations::replacementIsOneEdit() {
	auto editor = Editor();
	auto* document = editor.text();
	document->setPlainText(QStringLiteral("c\nb\na\nd\ne"));
	auto spy = QSignalSpy(&editor.decorations, &TextDecorations::contentsChanged);
	auto positions = QList<qint32>();
	QObject::connect(
	    &editor.decorations,
	    &TextDecorations::contentsChanged,
	    &editor.decorations,
	    [&]() { positions.append(editor.decorations.undoSteps()); }
	);

	editor.decorations.replace(0, 5, QStringLiteral("a\nb\nc"));
	QCOMPARE(document->toPlainText(), QStringLiteral("a\nb\nc\nd\ne"));
	QCOMPARE(spy.count(), 1);
	QCOMPARE(spy.at(0), (QVariantList {0, 5, 5}));

	document->undo();
	QCOMPARE(document->toPlainText(), QStringLiteral("c\nb\na\nd\ne"));
	QCOMPARE(spy.count(), 2);
	QVERIFY(!document->isUndoAvailable());
	document->redo();
	QCOMPARE(document->toPlainText(), QStringLiteral("a\nb\nc\nd\ne"));
	QCOMPARE(spy.count(), 3);
	QVERIFY(positions.at(0) > 0);
	QCOMPARE(positions.at(1), 0);
	QCOMPARE(positions.at(2), positions.at(0));
}

void TestTextDecorations::replacementClampsUtf16Ranges() {
	auto detached = TextDecorations();
	detached.replace(0, 10, QStringLiteral("ignored"));

	auto editor = Editor();
	auto* document = editor.text();
	document->setPlainText(QStringLiteral("a\U0001f600b"));
	editor.decorations.replace(1, 3, QStringLiteral("X"));
	QCOMPARE(document->toPlainText(), QStringLiteral("aXb"));
	editor.decorations.replace(-5, 1, QString());
	QCOMPARE(document->toPlainText(), QStringLiteral("Xb"));
	editor.decorations.replace(100, 200, QStringLiteral("\nc"));
	QCOMPARE(document->toPlainText(), QStringLiteral("Xb\nc"));
	editor.decorations.replace(1, -1, QStringLiteral("!"));
	QCOMPARE(document->toPlainText(), QStringLiteral("X!b\nc"));
}

QTEST_MAIN(TestTextDecorations);
