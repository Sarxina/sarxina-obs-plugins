#include "countdown-picture.hpp"

#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace sarxina::keeptalking {

namespace {

const QColor kFill(0xFF, 0xB3, 0x47);    // amber
const QColor kNavy(0x1F, 0x23, 0x40); // navy, for the outline and the shadow

constexpr double kDigitHeight = 0.45; // of the canvas's height
constexpr double kOutlineWidth = 0.087;    // of the font's size, as on the captions (8 px at 92 px)
constexpr double kShadow = 0.11;      // straight down, of the font's size (10 px at 92 px)
constexpr double kLineHeight = 0.12;  // the line over the number, of the canvas's height
constexpr double kLineMaxWidth = 0.92; // of the canvas's width, at most
constexpr const char *kLine = "START YAPPING OR ELSE";

} // namespace

QString countdownFont()
{
	static const QString family = [] {
		const QStringList families = QFontDatabase::families();
		for (const char *wanted : {"Luckiest Guy", "Arial Black"})
			if (families.contains(QString::fromLatin1(wanted), Qt::CaseInsensitive))
				return QString::fromLatin1(wanted);
		return QStringLiteral("Arial");
	}();
	return family;
}

QImage paintNumber(int number, int canvasWidth, int canvasHeight)
{
	const QString family = countdownFont();
	const bool bold = family != QStringLiteral("Luckiest Guy"); // Luckiest Guy is bold already

	// The number, and the line above it, each a path at its own size.
	const int digitSize = std::max(16, static_cast<int>(std::lround(canvasHeight * kDigitHeight)));
	QFont digitFont(family);
	digitFont.setPixelSize(digitSize);
	digitFont.setBold(bold);
	QPainterPath digit;
	digit.addText(0, 0, digitFont, QString::number(number));

	int lineSize = std::max(12, static_cast<int>(std::lround(canvasHeight * kLineHeight)));
	QFont lineFont(family);
	lineFont.setBold(bold);
	QPainterPath line;
	for (;;) { // as big as it may be, but inside the canvas's width
		lineFont.setPixelSize(lineSize);
		line = QPainterPath();
		line.addText(0, 0, lineFont, QString::fromLatin1(kLine));
		const double outer = line.boundingRect().width() + 2 * lineSize * kOutlineWidth;
		if (outer <= canvasWidth * kLineMaxWidth || lineSize <= 12)
			break;
		lineSize = std::max(12, static_cast<int>(lineSize * 0.95));
	}

	struct Part {
		QPainterPath path;
		double outline, shadow;
	};
	// The line centred over the number, a little apart.
	const QRectF lineInk = line.boundingRect(), digitInk = digit.boundingRect();
	const double gap = lineSize * 0.45 + lineSize * kShadow;
	line.translate(-lineInk.center().x(), -lineInk.bottom());
	digit.translate(-digitInk.center().x(), gap - digitInk.top());
	const Part parts[] = {{line, lineSize * kOutlineWidth, lineSize * kShadow},
			      {digit, digitSize * kOutlineWidth, digitSize * kShadow}};

	QRectF ink;
	for (const Part &part : parts)
		ink = ink.united(part.path.boundingRect().adjusted(-part.outline, -part.outline, part.outline,
								   part.outline + part.shadow));
	const int margin = 2;
	QImage picture(static_cast<int>(std::ceil(ink.width())) + 2 * margin,
		       static_cast<int>(std::ceil(ink.height())) + 2 * margin, QImage::Format_ARGB32_Premultiplied);
	picture.fill(Qt::transparent);

	QPainter painter(&picture);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(margin - ink.left(), margin - ink.top());
	for (const Part &part : parts) {
		const QPen pen(kNavy, part.outline * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		// The shadow: the outlined text again, straight down.
		painter.save();
		painter.translate(0, part.shadow);
		painter.setPen(pen);
		painter.setBrush(kNavy);
		painter.drawPath(part.path);
		painter.restore();
		// The outline, then the fill over its inner half.
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		painter.drawPath(part.path);
		painter.setPen(Qt::NoPen);
		painter.setBrush(kFill);
		painter.drawPath(part.path);
	}
	return picture;
}
} // namespace sarxina::keeptalking
