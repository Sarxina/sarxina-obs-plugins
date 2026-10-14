#include "panel-painter.hpp"

#include "panel-colors.hpp"
#include "game.hpp"

#include <obs-module.h>

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QRawFont>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sarxina {

namespace {

constexpr double kHeadDrawRadius = kHeadRadius + 3; // the picture is drawn a little larger than the head that hits
constexpr double kLabelMargin = 5.0;                // the least room between a label and the ends of its button
constexpr double kShadowBlur = 4.0;                 // device pixels
constexpr double kShadowDrop = 3.0;                 // device pixels
constexpr double kShadowOpacity = 0.45;
constexpr double kToastMs = 1900.0;
constexpr double kTextHairline = 0.3; // device pixels
constexpr Point kGear{kWidth - 16, 14};
constexpr double kGearReach = 13;

double degrees(double radians)
{
	return radians * 180.0 / 3.14159265358979323846;
}

QString clockText(double ms)
{
	const qint64 s = static_cast<qint64>(std::floor(std::max(0.0, ms) / 1000.0));
	return QStringLiteral("%1:%2:%3")
		.arg(s / 3600, 2, 10, QLatin1Char('0'))
		.arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
		.arg(s % 60, 2, 10, QLatin1Char('0'));
}

// Text is placed by the middle of the font's em square. This finds how far below that middle the baseline sits.
double baselineBelowMiddle(const QFont &font)
{
	double ascent = 0.776, descent = 0.224; // Segoe UI, in case the font's own table cannot be read
	const QByteArray os2 = QRawFont::fromFont(font).fontTable("OS/2");
	if (os2.size() >= 72) {
		const double up = qFromBigEndian<qint16>(os2.constData() + 68);
		const double down = -static_cast<double>(qFromBigEndian<qint16>(os2.constData() + 70));
		if (up > 0 && down >= 0) {
			ascent = up / (up + down);
			descent = down / (up + down);
		}
	}
	return (ascent - descent) / 2.0 * font.pixelSize();
}

QFont panelFont(const QStringList &families, int pixels, QFont::Weight weight = QFont::Normal)
{
	QFont font;
	font.setFamilies(families);
	font.setPixelSize(pixels);
	font.setWeight(weight);
	font.setHintingPreference(QFont::PreferNoHinting); // the panel is scaled, so text is drawn from its outlines
	return font;
}

// Text drawn as outlines, so it scales, turns and moves by fractions of a pixel with the thing it sits on.
void drawText(QPainter &g, const QString &string, const QFont &font, double baseline, double x, double y,
	      Qt::Alignment align, const QColor &color)
{
	const double width = QFontMetricsF(font).horizontalAdvance(string);
	double left = x;
	if (align & Qt::AlignHCenter)
		left -= width / 2;
	else if (align & Qt::AlignRight)
		left -= width;
	QPainterPath path;
	path.addText(QPointF(left, y + baseline), font, string);
	// Plain outlines come out a touch lighter than text normally does on screen; a hairline around them evens it out.
	const QTransform &device = g.deviceTransform();
	const double deviceScale = std::hypot(device.m11(), device.m12());
	if (deviceScale > 0)
		g.strokePath(path, QPen(color, kTextHairline / deviceScale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	g.fillPath(path, color);
}

// Gaussian blur of one channel, edges treated as empty.
void blur(std::vector<float> &pixels, int width, int height, double sigma)
{
	const int radius = static_cast<int>(std::ceil(sigma * 3));
	std::vector<float> kernel(static_cast<size_t>(radius) * 2 + 1);
	float total = 0;
	for (int i = -radius; i <= radius; i++) {
		const float k = static_cast<float>(std::exp(-(i * i) / (2 * sigma * sigma)));
		kernel[static_cast<size_t>(i + radius)] = k;
		total += k;
	}
	for (float &k : kernel)
		k /= total;

	std::vector<float> pass(pixels.size());
	const auto at = [width](int x, int y) { return static_cast<size_t>(y) * width + x; };
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0;
			for (int i = std::max(-radius, -x); i <= std::min(radius, width - 1 - x); i++)
				sum += kernel[static_cast<size_t>(i + radius)] * pixels[at(x + i, y)];
			pass[at(x, y)] = sum;
		}
	}
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0;
			for (int i = std::max(-radius, -y); i <= std::min(radius, height - 1 - y); i++)
				sum += kernel[static_cast<size_t>(i + radius)] * pass[at(x, y + i)];
			pixels[at(x, y)] = sum;
		}
	}
}

} // namespace

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

PanelPainter::PanelPainter() : head_(QStringLiteral(":/sarxina/head.png"))
{
	const QStringList family{QStringLiteral("Segoe UI"), QStringLiteral("Open Sans")};
	label_.font = panelFont(family, 13);
	heading_.font = panelFont(family, 12, QFont::DemiBold);
	toast_.font = panelFont(family, 13, QFont::DemiBold);
	status_.font = panelFont(family, 11);
	gear_.font = panelFont({QStringLiteral("Segoe UI Symbol"), QStringLiteral("Segoe UI")}, 15);
	for (Face *face : {&label_, &heading_, &toast_, &status_, &gear_})
		face->baseline = baselineBelowMiddle(face->font);
	for (const ControlInfo &control : kControls) {
		for (const char *label : {control.label, control.labelOn})
			widestLabel_ = std::max(widestLabel_, QFontMetricsF(label_.font).horizontalAdvance(text(label)));
	}
}

bool PanelPainter::overGear(Point p)
{
	return std::hypot(p.x - kGear.x, p.y - kGear.y) < kGearReach;
}

const QImage &PanelPainter::headAt(int devicePixels)
{
	const int size = std::max(8, devicePixels);
	if (headScaled_.width() != size)
		headScaled_ = head_.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	return headScaled_;
}

const QImage &PanelPainter::buttonShadow(double buttonW, double deviceScale)
{
	if (!shadow_.isNull() && std::abs(shadowScale_ - deviceScale) < 1e-6 && shadowWidth_ == buttonW)
		return shadow_;

	// The button's shape in device pixels, with room around it for the blur.
	const int margin = static_cast<int>(std::ceil(kShadowBlur * 3)) + 2;
	const int width = static_cast<int>(std::ceil(buttonW * deviceScale)) + margin * 2;
	const int height = static_cast<int>(std::ceil(kButtonH * deviceScale)) + margin * 2;
	QImage shape(width, height, QImage::Format_ARGB32_Premultiplied);
	shape.fill(Qt::transparent);
	{
		QPainter g(&shape);
		g.setRenderHint(QPainter::Antialiasing);
		g.setPen(Qt::NoPen);
		g.setBrush(Qt::black);
		g.drawRoundedRect(QRectF((width - buttonW * deviceScale) / 2, (height - kButtonH * deviceScale) / 2,
					 buttonW * deviceScale, kButtonH * deviceScale),
				  4 * deviceScale, 4 * deviceScale);
	}

	std::vector<float> alpha(static_cast<size_t>(width) * height);
	for (int y = 0; y < height; y++) {
		const QRgb *line = reinterpret_cast<const QRgb *>(shape.constScanLine(y));
		for (int x = 0; x < width; x++)
			alpha[static_cast<size_t>(y) * width + x] = qAlpha(line[x]) / 255.0f;
	}
	blur(alpha, width, height, kShadowBlur);

	shadow_ = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	for (int y = 0; y < height; y++) {
		QRgb *line = reinterpret_cast<QRgb *>(shadow_.scanLine(y));
		for (int x = 0; x < width; x++) {
			const float a = alpha[static_cast<size_t>(y) * width + x] * static_cast<float>(kShadowOpacity);
			line[x] = qRgba(0, 0, 0, std::clamp(static_cast<int>(std::lround(a * 255)), 0, 255));
		}
	}
	shadowScale_ = deviceScale;
	shadowWidth_ = buttonW;
	return shadow_;
}

void PanelPainter::paintButton(QPainter &g, const ControlPanel &panel, const Button &button, double deviceScale)
{
	const double width = panel.buttonWidth();
	// The shadow falls straight down on the screen, however the button is turned.
	const QImage &shadow = buttonShadow(width, deviceScale);
	const double shadowW = shadow.width() / deviceScale, shadowH = shadow.height() / deviceScale;
	g.save();
	g.translate(button.x, button.y + kShadowDrop / deviceScale);
	g.rotate(degrees(button.angle));
	g.drawImage(QRectF(-shadowW / 2, -shadowH / 2, shadowW, shadowH), shadow);
	g.restore();

	const bool flash = button.flash > 0;
	g.save();
	g.translate(button.x, button.y);
	g.rotate(degrees(button.angle));
	g.setPen(QPen(kButtonEdgeColor, 1));
	g.setBrush(flash ? QColor(Qt::white) : panel.isOn(button) ? kButtonOnColor : kButtonColor);
	g.drawRoundedRect(QRectF(-width / 2, -kButtonH / 2, width, kButtonH), 4, 4);
	// On a narrow button the labels shrink, all of them alike, by what the longest one needs to fit.
	if (const double fit = (width - 2 * kLabelMargin) / widestLabel_; fit < 1)
		g.scale(fit, fit);
	drawText(g, text(panel.label(button)), label_.font, label_.baseline, 0, 1, Qt::AlignHCenter,
		 flash ? kFlashTextColor : kTextColor);
	g.restore();
}

void PanelPainter::paintHead(QPainter &g, const Head &head, double deviceScale)
{
	const QImage &picture = headAt(static_cast<int>(std::ceil(kHeadDrawRadius * 2 * deviceScale)));
	g.save();
	g.translate(head.x, head.y);
	g.rotate(degrees(head.rotation));
	g.drawImage(QRectF(-kHeadDrawRadius, -kHeadDrawRadius, kHeadDrawRadius * 2, kHeadDrawRadius * 2), picture);
	g.restore();
}

void PanelPainter::paintToast(QPainter &g, const ControlPanel &panel, double now)
{
	const char *toast = panel.toast();
	if (!toast || now - panel.toastAt() >= kToastMs)
		return;
	const QString message = text(toast);
	const double width = QFontMetricsF(toast_.font).horizontalAdvance(message) + 22;
	const double top = panel.game().toastTop();
	g.setOpacity(std::clamp((kToastMs - (now - panel.toastAt())) / 300.0, 0.0, 1.0));
	g.setPen(Qt::NoPen);
	g.setBrush(QColor(0, 0, 0, 153));
	g.drawRoundedRect(QRectF(kWidth / 2 - width / 2, top, width, 24), 12, 12);
	drawText(g, message, toast_.font, toast_.baseline, kWidth / 2, top + 13, Qt::AlignHCenter, Qt::white);
	g.setOpacity(1);
}

// The status strip, like the bar along the bottom of OBS.
void PanelPainter::paintStatus(QPainter &g, const ControlPanel &panel, double now)
{
	const double W = kWidth, H = panel.height();
	g.fillRect(QRectF(0, H - 20, W, 20), kStatusBarColor);
	double x = 10;
	const auto light = [&](const QColor &color, const QString &words) {
		g.setPen(Qt::NoPen);
		g.setBrush(color);
		g.drawEllipse(QPointF(x + 4, H - 10), 4, 4);
		drawText(g, words, status_.font, status_.baseline, x + 13, H - 9, Qt::AlignLeft, kTextColor);
		x += 22 + QFontMetricsF(status_.font).horizontalAdvance(words);
	};
	const PanelState &state = panel.state();
	const bool streaming = state.isOn(Control::Stream), recording = state.isOn(Control::Record);
	const bool live = streaming && !state.connecting;
	light(live ? kLiveColor : kIdleColor,
	      live ? text("Status.Live") + QLatin1Char(' ') + clockText(now - state.onSince(Control::Stream))
	      : streaming ? text("Status.Connecting")
			  : text("Status.Offline"));
	light(recording ? kLiveColor : kIdleColor,
	      recording ? text("Status.Recording") + QLatin1Char(' ') + clockText(now - state.onSince(Control::Record))
			: text("Status.NotRecording"));

	QString hint = text(panel.game().hint());
	if (panel.practice())
		hint += QStringLiteral("   ") + text("Hint.Practice");
	drawText(g, hint, status_.font, status_.baseline, W - 10, H - 9, Qt::AlignRight, kDimColor);
}

// The panel powering off like an old television: two bars close to a white line, the line shrinks to a dot, the dot
// fades, and after a while the picture comes back.
void PanelPainter::paintPowerOff(QPainter &g, const ControlPanel &panel, double now) const
{
	if (!panel.poweredOff())
		return;
	const double W = kWidth, H = panel.height();
	const double t = (now - panel.poweredOffAt()) / 1000.0;
	g.setPen(Qt::NoPen);
	if (t > 0.5 && t < 3.3) {
		const double closed = std::min(1.0, (t - 0.5) / 0.28);
		const double bar = H * closed / 2;
		g.fillRect(QRectF(0, 0, W, closed >= 1 ? H : bar), Qt::black);
		g.fillRect(QRectF(0, H - bar, W, bar), Qt::black);
		if (closed >= 1) {
			const double shrunk = std::min(1.0, (t - 0.78) / 0.25);
			g.fillRect(QRectF(W / 2 * shrunk, H / 2 - 1, W * (1 - shrunk), 2), Qt::white);
			if (t < 1.3) {
				g.setOpacity(std::clamp(1.0 - (t - 1.03) / 0.27, 0.0, 1.0));
				g.setBrush(Qt::white);
				g.drawEllipse(QPointF(W / 2, H / 2), 3, 3);
				g.setOpacity(1);
			}
		}
	}
	if (t >= 3.3) {
		g.setOpacity(std::clamp(1.0 - (t - 3.3) / 0.3, 0.0, 1.0));
		g.fillRect(QRectF(0, 0, W, H), Qt::black);
		g.setOpacity(1);
	}
}

void PanelPainter::paint(QPainter &g, const ControlPanel &panel, double now, double scale)
{
	if (scale <= 0)
		return;
	const double W = kWidth, H = panel.height();
	const double deviceScale = scale * g.device()->devicePixelRatioF();
	const Game &game = panel.game();

	g.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	g.scale(scale, scale);
	g.fillRect(QRectF(0, 0, W, H), kPanelColor);

	g.save(); // everything from here to the matching restore shakes on a hit
	if (const double shake = panel.shake() * 7; shake > 0) {
		QRandomGenerator *random = QRandomGenerator::global();
		g.translate((random->generateDouble() - 0.5) * shake, (random->generateDouble() - 0.5) * shake);
	}

	drawText(g, text("Controls"), heading_.font, heading_.baseline, 10, 14, Qt::AlignLeft, kDimColor);
	drawText(g, QString(QChar(0x2699)), gear_.font, gear_.baseline, kGear.x, kGear.y + 1, Qt::AlignHCenter,
		 kDimColor);
	g.fillRect(QRectF(0, panel.ground(), W, H - panel.ground()), kGroundColor);

	game.paintBehind(g, panel);
	for (const Button &button : panel.buttons())
		paintButton(g, panel, button, deviceScale);
	if (game.showsHead(panel.head()))
		paintHead(g, panel.head(), deviceScale);
	game.paintFront(g, panel);

	for (const Particle &particle : panel.particles()) {
		g.setOpacity(std::clamp(1.0 - particle.age / particle.life, 0.0, 1.0));
		g.fillRect(QRectF(particle.x - 2, particle.y - 2, 4, 4), QColor(QRgb(particle.rgb)));
	}
	g.setOpacity(1);
	paintToast(g, panel, now);
	g.restore();

	paintStatus(g, panel, now);
	paintPowerOff(g, panel, now);
}

} // namespace sarxina
