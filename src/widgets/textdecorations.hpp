#pragma once

#include <qcolor.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qquicktextdocument.h>
#include <qtextdocument.h>
#include <qtextformat.h>
#include <qtextobject.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::widgets {

///! Per-line formats and edit deltas for a TextEdit.
/// Draws syntax colors, spelling underlines and search matches into a plain-text
/// @@QtQuick.TextEdit's own layout, without touching its text or undo history,
/// and reports each edit as a single replacement.
///
/// Formats are set one line (text block) at a time, so a caller only pays for
/// the lines it shows. A line keeps its formats until they are set again or
/// @@clear() is called.
///
/// #### Example
/// ```qml
/// TextEdit {
///   id: editor
///   textFormat: TextEdit.PlainText
/// }
///
/// TextDecorations {
///   document: editor.textDocument
///   palette: [{}, { color: "#ff79c6", bold: true }]
///   onContentsChanged: (position, removed, added) => console.log(text(position, position + added))
///   Component.onCompleted: setLine(0, [0, 6, 1], [], [])
/// }
/// ```
class TextDecorations: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	/// The @@QtQuick.TextEdit.textDocument to decorate.
	Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged);
	// clang-format off
	/// Formats for the roles in @@setLine()'s spans: one `{ color, bold, italic }` object per role id.
	/// A missing or empty entry keeps the editor's own format.
	Q_PROPERTY(QVariantList palette READ palette WRITE setPalette NOTIFY paletteChanged);
	// clang-format on
	/// Underline color for spelling ranges.
	Q_PROPERTY(QColor spellColor READ spellColor WRITE setSpellColor NOTIFY spellColorChanged);
	/// Background color for match ranges.
	Q_PROPERTY(QColor matchColor READ matchColor WRITE setMatchColor NOTIFY matchColorChanged);

public:
	explicit TextDecorations(QObject* parent = nullptr): QObject(parent) {}

	// clang-format off
	/// Replaces the formats of line `line`. `spans` is `[column, length, role, ...]`, `spell` and `matches`
	/// are `[column, length, ...]`, with columns in UTF-16 units. Returns true when the formats changed.
	Q_INVOKABLE bool setLine(qint32 line, const QList<qint32>& spans, const QList<qint32>& spell, const QList<qint32>& matches);
	/// Removes every format from the document.
	Q_INVOKABLE void clear();
	/// The text in `[start, end)` as stored: line breaks as `\n`, while no-break spaces and line separators
	/// stay as they are (@@QtQuick.TextEdit.getText() turns them into spaces and line breaks).
	Q_INVOKABLE [[nodiscard]] QString text(qint32 start, qint32 end) const;
	/// Replaces `[start, end)` in UTF-16 units as one undo step and one edit delta.
	Q_INVOKABLE void replace(qint32 start, qint32 end, const QString& text);
	/// The current Qt undo stack position. Read it in @@contentsChanged(s) to identify an edit.
	Q_INVOKABLE [[nodiscard]] qint32 undoSteps() const;
	// clang-format on

	[[nodiscard]] QQuickTextDocument* document() const { return this->mDocument; }
	void setDocument(QQuickTextDocument* document);

	[[nodiscard]] QVariantList palette() const { return this->mPalette; }
	void setPalette(const QVariantList& palette);

	[[nodiscard]] QColor spellColor() const { return this->mSpellColor; }
	void setSpellColor(const QColor& color);

	[[nodiscard]] QColor matchColor() const { return this->mMatchColor; }
	void setMatchColor(const QColor& color);

signals:
	/// The text changed: `removed` UTF-16 units at `position` were replaced by `added` units.
	/// Emitted once per edit, undo and redo included, after the document holds the new text.
	void contentsChanged(qint32 position, qint32 removed, qint32 added);
	void documentChanged();
	void paletteChanged();
	void spellColorChanged();
	void matchColorChanged();

private slots:
	void onTextDocumentChanged();
	void onContentsChange(qint32 position, qint32 removed, qint32 added);

private:
	void attach(QTextDocument* textDocument);
	void markDirty(const QTextBlock& block);
	[[nodiscard]] qint32 textLength() const;

	QPointer<QQuickTextDocument> mDocument;
	// The TextEdit owns both documents; QPointer drops them if it is destroyed first.
	QPointer<QTextDocument> mTextDocument;
	QVariantList mPalette;
	QList<QTextCharFormat> mRoleFormats;
	QColor mSpellColor;
	QColor mMatchColor;
	qint32 mLength = 0;
	bool mHasFormats = false;
};

} // namespace qs::widgets
