#include "textdecorations.hpp"
#include <algorithm>

#include <qabstracttextdocumentlayout.h>
#include <qcolor.h>
#include <qcontainerfwd.h>
#include <qlist.h>
#include <qobject.h>
#include <qquicktextdocument.h>
#include <qstring.h>
#include <qtextcursor.h>
#include <qtextdocument.h>
#include <qtextformat.h>
#include <qtextlayout.h>
#include <qtextobject.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::widgets {

namespace {

QTextCharFormat roleFormat(const QVariant& entry) {
	auto map = entry.toMap();
	auto format = QTextCharFormat();

	auto color = map.value(QStringLiteral("color")).value<QColor>();
	if (color.isValid()) format.setForeground(color);
	if (map.value(QStringLiteral("bold")).toBool()) format.setFontWeight(QFont::Bold);
	if (map.value(QStringLiteral("italic")).toBool()) format.setFontItalic(true);

	return format;
}

} // namespace

void TextDecorations::setDocument(QQuickTextDocument* document) {
	if (document == this->mDocument) return;

	if (this->mDocument) QObject::disconnect(this->mDocument, nullptr, this, nullptr);
	this->mDocument = document;

	if (document) {
		QObject::connect(
		    document,
		    &QQuickTextDocument::textDocumentChanged,
		    this,
		    &TextDecorations::onTextDocumentChanged
		);
	}

	this->attach(document ? document->textDocument() : nullptr);
	emit this->documentChanged();
}

void TextDecorations::onTextDocumentChanged() {
	this->attach(this->mDocument ? this->mDocument->textDocument() : nullptr);
}

void TextDecorations::attach(QTextDocument* textDocument) {
	if (textDocument == this->mTextDocument) return;

	if (this->mTextDocument) QObject::disconnect(this->mTextDocument, nullptr, this, nullptr);
	this->mTextDocument = textDocument;
	this->mLength = this->textLength();
	this->mHasFormats = true;

	if (textDocument) {
		QObject::connect(
		    textDocument,
		    &QTextDocument::contentsChange,
		    this,
		    &TextDecorations::onContentsChange
		);
	}
}

qint32 TextDecorations::textLength() const {
	// characterCount() includes the separator that closes the last block.
	return this->mTextDocument ? this->mTextDocument->characterCount() - 1 : 0;
}

void TextDecorations::onContentsChange(qint32 position, qint32 removed, qint32 added) {
	auto oldLength = this->mLength;
	auto length = this->textLength();
	this->mLength = length;

	// Replacing the whole document (setPlainText) also counts the separator that closes the last block.
	removed = std::min(removed, oldLength - position);
	added = std::min(added, length - position);

	if (removed == 0 && added == 0) return;
	emit this->contentsChanged(position, removed, added);
}

bool TextDecorations::setLine(
    qint32 line,
    const QList<qint32>& spans,
    const QList<qint32>& spell,
    const QList<qint32>& matches
) {
	if (!this->mTextDocument) return false;

	auto block = this->mTextDocument->findBlockByNumber(line);
	if (!block.isValid()) return false;

	auto size = static_cast<qint64>(block.length() - 1);
	auto formats = QList<QTextLayout::FormatRange>();

	auto add = [&](qint64 column, qint64 length, const QTextCharFormat& format) {
		auto start = std::clamp(column, static_cast<qint64>(0), size);
		auto end = std::clamp(column + length, start, size);
		if (end > start)
			formats.append(
			    {.start = static_cast<qint32>(start),
			     .length = static_cast<qint32>(end - start),
			     .format = format}
			);
	};

	for (qsizetype i = 0; i + 2 < spans.size(); i += 3) {
		auto role = spans.at(i + 2);
		if (role < 0 || role >= this->mRoleFormats.size()) continue;

		const auto& format = this->mRoleFormats.at(role);
		if (!format.properties().isEmpty()) add(spans.at(i), spans.at(i + 1), format);
	}

	if (!spell.isEmpty()) {
		auto format = QTextCharFormat();
		// Qt Quick draws single underlines only; a spell-check wave is skipped.
		format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
		format.setUnderlineColor(this->mSpellColor);
		for (qsizetype i = 0; i + 1 < spell.size(); i += 2) add(spell.at(i), spell.at(i + 1), format);
	}

	if (!matches.isEmpty()) {
		auto format = QTextCharFormat();
		format.setBackground(this->mMatchColor);
		for (qsizetype i = 0; i + 1 < matches.size(); i += 2)
			add(matches.at(i), matches.at(i + 1), format);
	}

	auto* layout = block.layout();
	if (layout->formats() == formats) return false;

	layout->setFormats(formats);
	this->mHasFormats = this->mHasFormats || !formats.isEmpty();
	this->markDirty(block);
	return true;
}

void TextDecorations::clear() {
	// Walking every block also creates its layout, so skip documents that never had a format.
	if (!this->mTextDocument || !this->mHasFormats) return;
	this->mHasFormats = false;

	for (auto block = this->mTextDocument->begin(); block.isValid(); block = block.next()) {
		auto* layout = block.layout();
		if (!layout || layout->formats().isEmpty()) continue;

		layout->clearFormats();
		this->markDirty(block);
	}
}

void TextDecorations::markDirty(const QTextBlock& block) {
	// Relays the block out without emitting contentsChange, so formats never read as edits.
	this->mTextDocument->markContentsDirty(block.position(), block.length());

	// TextEdit repaints a block on updateBlock only; markContentsDirty does not emit it.
	emit this->mTextDocument->documentLayout()->updateBlock(block);
}

QString TextDecorations::text(qint32 start, qint32 end) const {
	if (!this->mTextDocument) return QString();

	auto length = this->textLength();
	start = std::clamp(start, 0, length);
	end = std::clamp(end, start, length);
	if (start == end) return QString();

	auto cursor = QTextCursor(this->mTextDocument);
	cursor.setPosition(start);
	cursor.setPosition(end, QTextCursor::KeepAnchor);

	auto text = cursor.selectedText();
	text.replace(QChar::ParagraphSeparator, u'\n');
	return text;
}

void TextDecorations::setPalette(const QVariantList& palette) {
	if (palette == this->mPalette) return;

	this->mPalette = palette;
	this->mRoleFormats.clear();
	this->mRoleFormats.reserve(palette.size());
	for (const auto& entry: palette) this->mRoleFormats.append(roleFormat(entry));

	emit this->paletteChanged();
}

void TextDecorations::replace(qint32 start, qint32 end, const QString& text) {
	if (!this->mTextDocument) return;

	auto length = this->textLength();
	start = std::clamp(start, 0, length);
	end = std::clamp(end, start, length);
	if (start == end && text.isEmpty()) return;

	auto cursor = QTextCursor(this->mTextDocument);
	cursor.setPosition(start);
	cursor.setPosition(end, QTextCursor::KeepAnchor);
	cursor.beginEditBlock();
	cursor.insertText(text);
	cursor.endEditBlock();
}

qint32 TextDecorations::undoSteps() const {
	return this->mTextDocument ? this->mTextDocument->availableUndoSteps() : 0;
}

void TextDecorations::setSpellColor(const QColor& color) {
	if (color == this->mSpellColor) return;
	this->mSpellColor = color;
	emit this->spellColorChanged();
}

void TextDecorations::setMatchColor(const QColor& color) {
	if (color == this->mMatchColor) return;
	this->mMatchColor = color;
	emit this->matchColorChanged();
}

} // namespace qs::widgets
