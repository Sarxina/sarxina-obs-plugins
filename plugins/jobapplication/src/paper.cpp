#include "paper.hpp"

#include <QBuffer>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRawFont>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace sarxina::jobapplication {

namespace {

constexpr int kMargin = 44; // around the sheet, for its shadow
constexpr double kPi = 3.14159265358979323846;

// The same numbers for a seed on every computer, which the standard library's distributions do not promise.
struct Random {
	uint32_t state;
	explicit Random(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}
	uint32_t next()
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	double between(double low, double high) { return low + (high - low) * (next() / 4294967296.0); }
};

// The ink.
const QColor kPaper(246, 242, 232);
const QColor kText(38, 42, 51);
const QColor kRule(92, 98, 110);
const QColor kBar(201, 211, 227);
const QColor kBarText(31, 42, 61);
const QColor kHand(27, 42, 110); // a blue pen, for what is written in
const QColor kPencil(84, 88, 98); // who wrote each line of experience, signed in pencil

// The handwriting, and after it fonts that have what it lacks: emoji, symbols, and the scripts Windows has fonts for.
// Qt takes a character from the first of these that has it.
const QStringList kHandFonts{QStringLiteral("Segoe Print"),        QStringLiteral("Ink Free"),
			     QStringLiteral("Comic Sans MS"),      QStringLiteral("Segoe UI Emoji"),
			     QStringLiteral("Segoe UI Symbol"),    QStringLiteral("Yu Gothic UI"),
			     QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft JhengHei UI"),
			     QStringLiteral("Malgun Gothic"),      QStringLiteral("Nirmala UI"),
			     QStringLiteral("Leelawadee UI"),      QStringLiteral("Ebrima"),
			     QStringLiteral("Gadugi"),             QStringLiteral("Segoe UI Historic"),
			     QStringLiteral("Segoe UI")};

QFont printed(double pixels, bool bold = false, bool italic = false)
{
	QFont font;
	font.setFamilies({QStringLiteral("Arial"), QStringLiteral("Helvetica"), QStringLiteral("Liberation Sans")});
	font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixels))));
	font.setBold(bold);
	font.setItalic(italic);
	font.setStyleStrategy(QFont::PreferAntialias);
	return font;
}

QFont handwritten(double pixels)
{
	QFont font;
	font.setFamilies(kHandFonts);
	font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixels))));
	font.setStyleStrategy(QFont::PreferAntialias);
	return font;
}

// A noise picture, `cells` wide, drawn over the sheet scaled up and multiplied in: light values only, in steps, so the
// grain stays faint and the PNG small.
void multiplyNoise(QPainter &painter, const QRectF &onto, int cellsWide, int low, int step, double opacity, Random &random)
{
	const int wide = std::max(2, cellsWide);
	const int high = std::max(2, static_cast<int>(std::lround(cellsWide * onto.height() / onto.width())));
	QImage noise(wide, high, QImage::Format_RGB32);
	const int levels = (255 - low) / step + 1;
	for (int y = 0; y < high; y++) {
		auto *row = reinterpret_cast<QRgb *>(noise.scanLine(y));
		for (int x = 0; x < wide; x++) {
			const int v = std::min(255, low + step * static_cast<int>(random.between(0, levels)));
			row[x] = qRgb(v, v, std::min(255, v + 2));
		}
	}
	painter.save();
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.setCompositionMode(QPainter::CompositionMode_Multiply);
	painter.setOpacity(opacity);
	painter.drawImage(onto, noise);
	painter.restore();
}

void fibres(QPainter &painter, const QSizeF &size, int count, Random &random)
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	for (int i = 0; i < count; i++) {
		const QPointF from(random.between(0, size.width()), random.between(0, size.height()));
		const double angle = random.between(0, 2 * kPi), length = random.between(5, 20);
		const QPointF to = from + QPointF(std::cos(angle), std::sin(angle)) * length;
		const QPointF bend = (from + to) / 2 + QPointF(random.between(-3, 3), random.between(-3, 3));
		QPainterPath path(from);
		path.quadTo(bend, to);
		painter.setPen(QPen(QColor(150, 138, 118, static_cast<int>(random.between(10, 30))), random.between(0.7, 1.4)));
		painter.drawPath(path);
	}
	painter.restore();
}

// Where the sheet was folded in three: a crease line with a highlight over it, and shade under it.
void fold(QPainter &painter, double y, double width)
{
	QLinearGradient shade(0, y, 0, y + 26);
	shade.setColorAt(0, QColor(0, 0, 0, 16));
	shade.setColorAt(1, QColor(0, 0, 0, 0));
	painter.fillRect(QRectF(0, y, width, 26), shade);
	painter.fillRect(QRectF(0, y - 1.6, width, 1.4), QColor(255, 255, 255, 110));
	painter.fillRect(QRectF(0, y, width, 1.1), QColor(70, 60, 45, 40));
}

// ---- the printed form -----------------------------------------------------------------------------------------------

class FormPainter {
public:
	FormPainter(QPainter &painter, double width, double height)
		: p_(painter), scale_(width / 1100.0), width_(width), height_(height), pad_(70 * scale_), content_(width - 2 * pad_)
	{
	}

	void paint(const Form &form)
	{
		p_.save();
		p_.setRenderHint(QPainter::Antialiasing);
		p_.setRenderHint(QPainter::TextAntialiasing);
		double y = 64 * s();
		title(form.title, y);
		y += 52 * s();
		centred(form.note, printed(20 * s(), false, true), y, QColor(kText.red(), kText.green(), kText.blue(), 210));
		y += 44 * s();
		for (const Section &section : form.sections)
			y = this->section(section, y) + 12 * s();
		const QFont footer = printed(17 * s());
		p_.setFont(footer);
		p_.setPen(kText);
		p_.drawText(QRectF(pad_, height_ - 58 * s(), content_, 24 * s()), Qt::AlignRight | Qt::AlignVCenter,
			    form.footer);
		p_.restore();
	}

private:
	double s() const { return scale_; }

	void title(const QString &text, double baseline)
	{
		double size = 58 * s();
		QFont font = printed(size, true);
		font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5 * s());
		while (QFontMetricsF(font).horizontalAdvance(text) > content_ && size > 10) {
			size -= 1;
			font.setPixelSize(static_cast<int>(size));
		}
		centred(text, font, baseline, kText);
	}

	void centred(const QString &text, const QFont &font, double baseline, const QColor &color)
	{
		p_.setFont(font);
		p_.setPen(color);
		const double w = QFontMetricsF(font).horizontalAdvance(text);
		p_.drawText(QPointF((width_ - w) / 2, baseline), text);
	}

	double section(const Section &section, double y)
	{
		const double bar = 40 * s();
		p_.fillRect(QRectF(pad_, y, content_, bar), kBar);
		QFont heading = printed(22 * s(), true);
		heading.setLetterSpacing(QFont::AbsoluteSpacing, 1.6 * s());
		p_.setFont(heading);
		p_.setPen(kBarText);
		p_.drawText(QRectF(pad_, y, content_, bar), Qt::AlignCenter, section.heading);
		y += bar + 14 * s();
		if (!section.note.isEmpty()) {
			p_.setFont(printed(18 * s(), false, true));
			p_.setPen(kText);
			p_.drawText(QRectF(pad_, y, content_, 26 * s()), Qt::AlignLeft | Qt::AlignVCenter, section.note);
			y += 32 * s();
		}
		for (const Row &row : section.rows)
			y = this->row(row, y);
		return y;
	}

	double row(const Row &row, double y)
	{
		const double gap = 26 * s();
		double x = pad_;
		double bottom = y;
		for (size_t i = 0; i < row.fields.size(); i++) {
			const Field &field = row.fields[i];
			const double width = field.share * content_ - (i + 1 < row.fields.size() ? gap : 0);
			switch (field.kind) {
			case Field::Kind::Line:
				bottom = std::max(bottom, line(field, x, y, width));
				break;
			case Field::Kind::Lines:
				bottom = std::max(bottom, lines(field, x, y, width));
				break;
			case Field::Kind::Choice:
				bottom = std::max(bottom, choice(field, x, y, width));
				break;
			case Field::Kind::Bullets:
				bottom = std::max(bottom, bullets(field, x, y, width));
				break;
			}
			x += width + gap;
		}
		return bottom;
	}

	QFont label() const { return printed(20 * s(), true); }

	// "FULL NAME:" and a line to write on.
	double line(const Field &field, double x, double y, double width)
	{
		const double baseline = y + 46 * s();
		const QFont font = label();
		const QString text = field.label.toUpper() + QLatin1Char(':');
		p_.setFont(font);
		p_.setPen(kText);
		p_.drawText(QPointF(x, baseline), text);
		const double from = x + QFontMetricsF(font).horizontalAdvance(text) + 10 * s();
		rule(from, x + width, baseline + 4 * s());
		if (!field.value.isEmpty())
			written(field.value, QPointF(from + 8 * s(), baseline - 2 * s()), x + width - from - 12 * s());
		return y + 68 * s();
	}

	// Ruled lines; what is written runs on from one to the next.
	double lines(const Field &field, double x, double y, double width)
	{
		const double spacing = 64 * s();
		QStringList words = field.value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		const QFont hand = handwritten(34 * s());
		const QFontMetricsF metrics(hand);
		for (int i = 0; i < field.lines; i++) {
			const double baseline = y + spacing * (i + 1) - 14 * s();
			rule(x, x + width, baseline + 4 * s());
			QString text;
			while (!words.isEmpty()) {
				const QString longer = text.isEmpty() ? words.front() : text + QLatin1Char(' ') + words.front();
				if (!text.isEmpty() && metrics.horizontalAdvance(longer) > width - 20 * s())
					break;
				text = longer;
				words.pop_front();
			}
			if (!text.isEmpty())
				written(text, QPointF(x + 10 * s(), baseline - 2 * s()), width - 20 * s());
		}
		return y + spacing * field.lines + 4 * s();
	}

	// Ruled lines with a bullet for each entry, the newest first: its text in pen and who wrote it in pencil, on one
	// line or two. An entry that does not fit in the lines left, and every older one, is not drawn: it has scrolled
	// off the bottom.
	double bullets(const Field &field, double x, double y, double width)
	{
		const double spacing = 56 * s();
		const auto baseline = [&](int line) { return y + spacing * (line + 1) - 14 * s(); };
		for (int i = 0; i < field.lines; i++)
			rule(x, x + width, baseline(i) + 4 * s());
		const double indent = 34 * s(), room = width - indent - 8 * s();
		int line = 0;
		for (const Bullet &bullet : field.bullets) {
			const QString whole =
				bullet.text + (bullet.by.isEmpty() ? QString() : QStringLiteral(" \u2014 ") + bullet.by);
			// On one line if it fits a little smaller, else as large as fits on two.
			std::unique_ptr<QTextLayout> laid;
			for (double size = 38 * s(); size >= 32 * s() && (!laid || laid->lineCount() > 1); size -= 2 * s())
				laid = layOut(whole, handwritten(size), room, static_cast<int>(bullet.text.size()));
			for (double size = 38 * s(); laid->lineCount() > 1; size -= 2 * s()) {
				laid = layOut(whole, handwritten(size), room, static_cast<int>(bullet.text.size()));
				if (laid->lineCount() <= 2 || size <= 22 * s())
					break;
			}
			const int lines = std::min(laid->lineCount(), 2);
			if (line + lines > field.lines)
				break;
			p_.setPen(Qt::NoPen);
			p_.setBrush(kHand);
			p_.drawEllipse(QPointF(x + 14 * s(), baseline(line) - 11 * s()), 4.5 * s(), 4.5 * s());
			p_.setBrush(Qt::NoBrush);
			p_.setPen(kHand);
			for (int i = 0; i < lines; i++) {
				const QTextLine text = laid->lineAt(i);
				text.draw(&p_, QPointF(x + indent, baseline(line + i) - 2 * s() - text.ascent() - text.y()));
			}
			line += lines;
		}
		return y + spacing * field.lines + 4 * s();
	}

	// Text wrapped to `room`, at word boundaries where it can be and anywhere where it cannot (a long word, or a
	// script written without spaces); from `penUpTo` on it is in pencil.
	static std::unique_ptr<QTextLayout> layOut(const QString &text, const QFont &font, double room, int penUpTo)
	{
		auto layout = std::make_unique<QTextLayout>(text, font);
		QTextOption option;
		option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		layout->setTextOption(option);
		QTextCharFormat pencil;
		pencil.setForeground(kPencil);
		layout->setFormats({QTextLayout::FormatRange{penUpTo, static_cast<int>(text.size()) - penUpTo, pencil}});
		layout->beginLayout();
		double at = 0;
		for (QTextLine line = layout->createLine(); line.isValid(); line = layout->createLine()) {
			line.setLineWidth(room);
			line.setPosition(QPointF(0, at));
			at += line.height();
		}
		layout->endLayout();
		return layout;
	}

	// "HOURS WANTED:" and a box for each option.
	double choice(const Field &field, double x, double y, double)
	{
		const double baseline = y + 46 * s();
		const QFont font = label();
		const QString text = field.label.toUpper() + QLatin1Char(':');
		p_.setFont(font);
		p_.setPen(kText);
		p_.drawText(QPointF(x, baseline), text);
		double at = x + QFontMetricsF(font).horizontalAdvance(text) + 22 * s();
		const QFont option = printed(20 * s());
		const double box = 22 * s();
		for (const QString &name : field.options) {
			const QRectF square(at, baseline - box + 2 * s(), box, box);
			p_.setPen(QPen(kRule, 1.6 * s()));
			p_.setBrush(Qt::NoBrush);
			p_.drawRect(square);
			if (name == field.value) {
				QPainterPath tick(square.topLeft() + QPointF(box * 0.15, box * 0.55));
				tick.lineTo(square.topLeft() + QPointF(box * 0.42, box * 0.85));
				tick.lineTo(square.topLeft() + QPointF(box * 1.05, box * -0.1));
				p_.setPen(QPen(kHand, 3.2 * s(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				p_.drawPath(tick);
			}
			p_.setFont(option);
			p_.setPen(kText);
			p_.drawText(QPointF(at + box + 9 * s(), baseline), name);
			at += box + 9 * s() + QFontMetricsF(option).horizontalAdvance(name) + 30 * s();
		}
		return y + 68 * s();
	}

	void rule(double from, double to, double y)
	{
		p_.setPen(QPen(kRule, 1.6 * s()));
		p_.drawLine(QPointF(from, y), QPointF(to, y));
	}

	// What was written in, in pen, standing on the line: as large as fits, smaller if it is long.
	void written(const QString &text, const QPointF &baseline, double room)
	{
		double size = 36 * s();
		QFont font = handwritten(size);
		while (QFontMetricsF(font).horizontalAdvance(text) > room && size > 12 * s()) {
			size -= 1;
			font.setPixelSize(static_cast<int>(size));
		}
		p_.setFont(font);
		p_.setPen(kHand);
		p_.drawText(baseline, QFontMetricsF(font).elidedText(text, Qt::ElideRight, room));
	}

	QPainter &p_;
	double scale_, width_, height_, pad_, content_;
};

} // namespace

QSize pictureSize()
{
	const int below = static_cast<int>(std::ceil((1 - kPinDown) * kSheetHeight)) + kMargin;
	return {kSheetWidth + 2 * kMargin, 2 * below};
}

QRectF sheetInPicture()
{
	const QSize picture = pictureSize();
	return {static_cast<double>(kMargin), picture.height() / 2.0 - kPinDown * kSheetHeight,
		static_cast<double>(kSheetWidth), static_cast<double>(kSheetHeight)};
}

QImage paintSheet(const Form &form, uint32_t seed)
{
	Random random(seed);
	QImage sheet(kSheetWidth, kSheetHeight, QImage::Format_ARGB32_Premultiplied);
	sheet.fill(kPaper);
	const QRectF whole(0, 0, kSheetWidth, kSheetHeight);
	QPainter painter(&sheet);

	// The paper: blotches, a grain and fibres
	multiplyNoise(painter, whole, 30, 236, 4, 0.55, random);
	multiplyNoise(painter, whole, kSheetWidth / 3, 243, 6, 0.45, random);
	fibres(painter, whole.size(), 520, random);

	FormPainter(painter, kSheetWidth, kSheetHeight).paint(form);

	// Folded in three, the middle third a touch darker; the edges and the bottom in a little shade, as a held sheet
	// curves away from the light.
	painter.fillRect(QRectF(0, kSheetHeight / 3.0, kSheetWidth, kSheetHeight / 3.0), QColor(0, 0, 0, 6));
	fold(painter, kSheetHeight / 3.0, kSheetWidth);
	fold(painter, 2 * kSheetHeight / 3.0, kSheetWidth);
	QLinearGradient across(0, 0, kSheetWidth, 0);
	across.setColorAt(0, QColor(0, 0, 0, 22));
	across.setColorAt(0.09, QColor(0, 0, 0, 0));
	across.setColorAt(0.91, QColor(0, 0, 0, 0));
	across.setColorAt(1, QColor(0, 0, 0, 26));
	painter.fillRect(whole, across);
	QLinearGradient down(0, 0, 0, kSheetHeight);
	down.setColorAt(0, QColor(255, 255, 255, 18));
	down.setColorAt(0.3, QColor(0, 0, 0, 0));
	down.setColorAt(1, QColor(0, 0, 0, 20));
	painter.fillRect(whole, down);
	painter.setPen(QPen(QColor(196, 188, 170), 2));
	painter.drawRect(whole.adjusted(1, 1, -1, -1));
	painter.end();
	return sheet;
}

QImage paintPicture(const Form &form, uint32_t seed)
{
	return paintPicture(paintSheet(form, seed));
}

QImage paintPicture(const QImage &sheet)
{
	const QSize size = pictureSize();
	const QRectF at = sheetInPicture();

	// Slightly out of square: the top a little narrower and not quite level, as a sheet held up by hand.
	const QPolygonF corners{at.topLeft() + QPointF(0.012 * at.width(), 3), at.topRight() + QPointF(-0.008 * at.width(), 0),
				at.bottomRight() + QPointF(0, -2), at.bottomLeft() + QPointF(2, 0)};
	QTransform warp;
	QTransform::quadToQuad(QPolygonF(QRectF(0, 0, sheet.width(), sheet.height())), corners, warp);

	// The shadow: the sheet's outline, down and to the right, blurred by painting it small and scaling it back up.
	constexpr int kBlur = 10;
	QImage shadow(size / kBlur, QImage::Format_ARGB32_Premultiplied);
	shadow.fill(Qt::transparent);
	{
		QPainter painter(&shadow);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.scale(1.0 / kBlur, 1.0 / kBlur);
		painter.translate(10, 16);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 0, 0, 120));
		painter.drawPolygon(corners);
	}
	shadow = shadow.scaled(shadow.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	QImage picture(size, QImage::Format_ARGB32_Premultiplied);
	picture.fill(Qt::transparent);
	QPainter painter(&picture);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.drawImage(QRectF(QPointF(0, 0), QSizeF(size)), shadow);
	painter.setTransform(warp);
	painter.drawImage(QPointF(0, 0), sheet);
	painter.end();
	return picture;
}

namespace {

// The paper's fonts that are on this computer, to ask which characters they have.
const std::vector<QRawFont> &handFonts()
{
	static const std::vector<QRawFont> fonts = [] {
		std::vector<QRawFont> found;
		for (const QString &family : kHandFonts)
			if (QFontDatabase::hasFamily(family)) {
				QFont font(family);
				font.setPixelSize(32);
				const QRawFont raw = QRawFont::fromFont(font);
				if (raw.isValid())
					found.push_back(raw);
			}
		return found;
	}();
	return fonts;
}

bool hidden(char32_t c)
{
	return c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0x061C || (c >= 0x200B && c <= 0x200F) ||
	       (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2069) || c == 0xFEFF;
}

bool pictograph(char32_t c)
{
	return c >= 0x1F000 || (c >= 0x2190 && c <= 0x2BFF) || c == 0x00A9 || c == 0x00AE || c == 0x203C || c == 0x2049 ||
	       c == 0x2122 || c == 0x2139 || c == 0x3030 || c == 0x303D || c == 0x3297 || c == 0x3299;
}

// What joins or changes the emoji before it: the zero-width joiner and the text and emoji variation selectors.
bool joiner(char32_t c)
{
	return c == 0x200D || c == 0xFE0E || c == 0xFE0F;
}

bool inked(char32_t c)
{
	if (c == U' ')
		return true;
	const std::vector<QRawFont> &fonts = handFonts();
	return std::any_of(fonts.begin(), fonts.end(), [c](const QRawFont &font) { return font.supportsCharacter(c); });
}

} // namespace

QString writable(const QString &text, int longest)
{
	const QList<uint> all = text.toUcs4();
	QString kept;
	bool afterEmoji = false;
	for (qsizetype i = 0; i < all.size(); i++) {
		const char32_t c = all[i];
		if (joiner(c)) { // only inside an emoji, which then stays one; a stray one goes
			const bool after = i + 1 < all.size() && pictograph(all[i + 1]);
			if (afterEmoji && (c != 0x200D || after))
				kept += QString::fromUcs4(&c, 1);
			continue;
		}
		const bool space = c == U'\t' || c == U'\n' || c == U'\r';
		if (!space && (hidden(c) || !inked(c))) {
			afterEmoji = false;
			continue;
		}
		const char32_t put = space ? U' ' : c;
		kept += QString::fromUcs4(&put, 1);
		afterEmoji = pictograph(c) || (afterEmoji && c >= 0x1F3FB && c <= 0x1F3FF);
	}
	kept = kept.simplified();

	// Cut between whole characters as a reader sees them.
	QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, kept);
	int count = 0;
	qsizetype cut = 0;
	for (qsizetype at = graphemes.toNextBoundary(); at > 0; at = graphemes.toNextBoundary()) {
		if (++count == longest - 1)
			cut = at;
		if (count > longest)
			return kept.left(cut).trimmed() + QChar(0x2026);
	}
	return kept;
}

QByteArray png(const QImage &picture, bool quick)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	picture.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG", quick ? 80 : -1);
	return bytes;
}

} // namespace sarxina::jobapplication
