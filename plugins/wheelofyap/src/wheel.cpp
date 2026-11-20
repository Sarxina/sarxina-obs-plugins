#include "wheel.hpp"

#include "plain-text.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <random>

namespace sarxina::wheelofyap {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 2 * kPi;

constexpr double kPull = 0.07;       // radians the wheel is pulled back before it goes
constexpr double kPullSeconds = 0.35;
constexpr double kEase = 3.6;        // how long the slowing down drags on
constexpr double kHubRadius = 200;
constexpr double kLabelEnd = kRadius - 72;  // where a label ends, from the centre
constexpr double kLabelStart = kHubRadius + 30;
constexpr double kLargestLabel = 74; // pixels

const QColor kPalette[] = {QColor(0xFF, 0x3B, 0x5C), QColor(0xFF, 0x8A, 0x00), QColor(0xF5, 0xC2, 0x00),
			   QColor(0x2E, 0xC4, 0xB6), QColor(0x3A, 0x86, 0xFF), QColor(0x83, 0x38, 0xEC),
			   QColor(0x06, 0xC2, 0x8F), QColor(0xFF, 0x4F, 0xD8)};
constexpr int kColours = sizeof(kPalette) / sizeof(kPalette[0]);
const QColor kGold(0xFF, 0xD7, 0x00);
const QColor kRim(0x1A, 0x12, 0x30);
const QColor kInk(0x10, 0x0A, 0x1C);

double wrap(double angle)
{
	angle = std::fmod(angle, kTau);
	return angle < 0 ? angle + kTau : angle;
}

double degrees(double radians)
{
	return radians * 180 / kPi;
}

QFont heavy(double pixels)
{
	QFont font(QStringLiteral("Segoe UI"));
	font.setWeight(QFont::Black);
	font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixels))));
	return font;
}

// White letters with a dark edge, readable on every colour.
void outlined(QPainter &painter, const QPointF &at, const QString &text, const QFont &font, const QColor &fill,
	      const QColor &edge, double edgeShare)
{
	QPainterPath path;
	path.addText(at, font, text);
	QPen pen(edge, font.pixelSize() * edgeShare, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	painter.strokePath(path, pen);
	painter.fillPath(path, fill);
}

// A slice as a path around the centre (0, 0), with the wheel at 0.
QPainterPath slicePath(double from, double to, double radius)
{
	QPainterPath path;
	path.moveTo(0, 0);
	const QRectF box(-radius, -radius, 2 * radius, 2 * radius);
	// Qt's arcs go counterclockwise in degrees; the wheel's angles go clockwise.
	path.arcTo(box, -degrees(from), -degrees(to - from));
	path.closeSubpath();
	return path;
}

} // namespace

QString topicText(const QString &typed, int longest)
{
	return plainText(QString(typed).replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified(), longest);
}

QStringList slicesOf(const std::vector<Topic> &topics)
{
	QStringList slices;
	for (const Topic &topic : topics) {
		const QString label = topicText(topic.label);
		if (topic.on && !label.isEmpty())
			slices.push_back(label);
	}
	return slices;
}

int Face::sliceAt(double angle) const
{
	angle = wrap(angle);
	const auto after = std::upper_bound(starts.begin(), starts.end(), angle);
	const int slice = static_cast<int>(after - starts.begin()) - 1;
	return std::clamp(slice, 0, std::max(0, count() - 1));
}

std::shared_ptr<const Face> paintFace(const QStringList &labels, const QString &forced, int forcedAt)
{
	auto face = std::make_shared<Face>();
	face->labels = labels;
	if (!forced.isEmpty()) {
		forcedAt = std::clamp(forcedAt, 0, static_cast<int>(labels.size()));
		face->labels.insert(forcedAt, topicText(forced));
		face->forced = forcedAt;
		face->forcedText = topicText(forced, kLongestForced);
	}
	if (face->labels.isEmpty())
		face->labels.push_back(QStringLiteral("No topics"));
	const int count = face->count();

	double weights = 0;
	for (int i = 0; i < count; i++)
		weights += i == face->forced ? kForcedWeight : 1;
	double at = 0;
	for (int i = 0; i < count; i++) {
		face->starts.push_back(at);
		at += kTau * (i == face->forced ? kForcedWeight : 1) / weights;
	}
	face->starts.push_back(kTau);

	int colour = 0;
	for (int i = 0; i < count; i++) {
		if (i == face->forced) {
			face->colours.push_back(kGold);
			continue;
		}
		QColor next = kPalette[colour++ % kColours];
		if (i == count - 1 && count > 1 && next == face->colours.front()) // no two of a colour side by side
			next = kPalette[colour++ % kColours];
		face->colours.push_back(next);
	}

	QImage disc(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
	disc.fill(Qt::transparent);
	QPainter painter(&disc);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);
	painter.translate(kCentre);

	// The rim, the slices, the lines between them and the pegs the pointer clicks past.
	painter.setPen(Qt::NoPen);
	painter.setBrush(kRim);
	painter.drawEllipse(QPointF(0, 0), kRadius + 46, kRadius + 46);
	for (int i = 0; i < count; i++) {
		QRadialGradient shade(QPointF(0, 0), kRadius);
		shade.setColorAt(0, face->colours[i].darker(135));
		shade.setColorAt(0.55, face->colours[i]);
		shade.setColorAt(1, face->colours[i].lighter(112));
		painter.setBrush(shade);
		painter.drawPath(slicePath(face->starts[i], face->starts[i + 1], kRadius));
	}
	painter.setPen(QPen(kInk, count > 60 ? 3 : 5));
	for (int i = 0; i < count && count > 1; i++) {
		const double a = face->starts[i];
		painter.drawLine(QPointF(0, 0), QPointF(std::cos(a) * kRadius, std::sin(a) * kRadius));
	}
	painter.setPen(QPen(kGold, 8));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(QPointF(0, 0), kRadius + 4, kRadius + 4);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(0xF4, 0xF0, 0xFF));
	for (int i = 0; i < count && count > 1; i++) {
		const double a = face->starts[i];
		painter.drawEllipse(QPointF(std::cos(a) * (kRadius + 25), std::sin(a) * (kRadius + 25)), 8, 8);
	}

	// Each label along its slice's middle, from the hub outwards, ending near the rim, as big as the slice lets it.
	for (int i = 0; i < count; i++) {
		const QString &label = face->labels[i];
		const double span = face->starts[i + 1] - face->starts[i];
		const QFontMetricsF reference(heavy(100));
		const double wide = std::max(1.0, reference.horizontalAdvance(label)); // at 100 pixels
		const double room = 0.88 * std::min(span, 1.2);
		double size = std::min({kLargestLabel, room * kLabelEnd / (0.82 + room * wide / 100),
					(kLabelEnd - kLabelStart) * 100 / wide});
		size = std::max(size, 10.0);
		const QFont font = heavy(size);
		const QFontMetricsF metrics(font);
		const double width = metrics.horizontalAdvance(label);
		painter.save();
		painter.rotate(degrees((face->starts[i] + face->starts[i + 1]) / 2));
		const double baseline = (metrics.ascent() - metrics.descent()) / 2;
		const bool gold = i == face->forced;
		outlined(painter, QPointF(kLabelEnd - width, baseline), label, font, gold ? kInk : Qt::white,
			 gold ? Qt::white : kInk, 0.2);
		painter.restore();
	}
	painter.end();
	face->disc = std::move(disc);
	return face;
}

QImage paintHub()
{
	const int side = static_cast<int>(2 * kHubRadius + 40);
	QImage hub(side, side, QImage::Format_ARGB32_Premultiplied);
	hub.fill(Qt::transparent);
	QPainter painter(&hub);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(side / 2.0, side / 2.0);
	QRadialGradient fill(QPointF(-40, -60), kHubRadius * 1.4);
	fill.setColorAt(0, QColor(0x4A, 0x2C, 0x86));
	fill.setColorAt(1, kRim);
	painter.setBrush(fill);
	painter.setPen(QPen(kGold, 14));
	painter.drawEllipse(QPointF(0, 0), kHubRadius, kHubRadius);
	const QFont font = heavy(84);
	const QFontMetricsF metrics(font);
	const QString top = QStringLiteral("WHEEL"), bottom = QStringLiteral("OF YAP");
	outlined(painter, QPointF(-metrics.horizontalAdvance(top) / 2, -8), top, font, Qt::white, kInk, 0.18);
	outlined(painter, QPointF(-metrics.horizontalAdvance(bottom) / 2, metrics.ascent() - 14), bottom, font, kGold,
		 kInk, 0.18);
	return hub;
}

QImage paintPointer()
{
	QImage pointer(220, 160, QImage::Format_ARGB32_Premultiplied);
	pointer.fill(Qt::transparent);
	QPainter painter(&pointer);
	painter.setRenderHint(QPainter::Antialiasing);
	QPainterPath arrow;
	arrow.moveTo(12, kPointerPivot.y());
	arrow.lineTo(kPointerPivot.x() + 10, kPointerPivot.y() - 62);
	arrow.quadTo(kPointerPivot.x() + 44, kPointerPivot.y(), kPointerPivot.x() + 10, kPointerPivot.y() + 62);
	arrow.closeSubpath();
	painter.setPen(QPen(Qt::white, 10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(QColor(0xFF, 0x2D, 0x55));
	painter.drawPath(arrow);
	painter.setPen(QPen(kInk, 6));
	painter.setBrush(Qt::white);
	painter.drawEllipse(kPointerPivot, 20, 20);
	return pointer;
}

QImage paintBanner(const QString &who, const QString &topic, bool forced, const QColor &colour)
{
	constexpr double kWidest = 1860, kTextWidth = 1720, kPad = 46;
	const QString by = (forced ? QStringLiteral("FORCED BY ") : QStringLiteral("SPUN BY ")) + who.toUpper();
	const QFont byFont = heavy(62);

	// The topic in as few lines as fit, at most two, as big as they can be.
	QStringList lines;
	QFont font;
	for (double size = 190; size >= 60; size -= 6) {
		font = heavy(size);
		const QFontMetricsF metrics(font);
		lines.clear();
		QString line;
		for (const QString &word : topic.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
			const QString longer = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
			if (metrics.horizontalAdvance(longer) <= kTextWidth || line.isEmpty()) {
				line = longer;
			} else {
				lines.push_back(line);
				line = word;
			}
		}
		if (!line.isEmpty())
			lines.push_back(line);
		const bool fits = std::all_of(lines.begin(), lines.end(), [&metrics](const QString &l) {
			return metrics.horizontalAdvance(l) <= kTextWidth;
		});
		if (lines.size() <= 2 && fits)
			break;
	}
	const QFontMetricsF metrics(font);
	for (QString &line : lines)
		line = metrics.elidedText(line, Qt::ElideRight, kTextWidth);
	while (lines.size() > 2)
		lines.pop_back();

	const double lineHeight = metrics.ascent() + metrics.descent() * 0.6;
	const QFontMetricsF byMetrics(byFont);
	const double height = kPad + byMetrics.ascent() + 26 + lineHeight * lines.size() + kPad;
	double width = byMetrics.horizontalAdvance(by);
	for (const QString &line : lines)
		width = std::max(width, metrics.horizontalAdvance(line));
	width = std::min(kWidest, std::max(900.0, width + 2 * kPad + 40));

	QImage banner(static_cast<int>(width) + 24, static_cast<int>(height) + 24, QImage::Format_ARGB32_Premultiplied);
	banner.fill(Qt::transparent);
	QPainter painter(&banner);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(12, 12);
	painter.setPen(QPen(colour, 14));
	painter.setBrush(QColor(0x12, 0x0C, 0x22, 240));
	painter.drawRoundedRect(QRectF(0, 0, width, height), 48, 48);
	double y = kPad + byMetrics.ascent();
	outlined(painter, QPointF((width - byMetrics.horizontalAdvance(by)) / 2, y), by, byFont,
		 forced ? kGold : colour.lighter(130), kInk, 0.12);
	y += 26;
	for (const QString &line : lines) {
		y += metrics.ascent();
		outlined(painter, QPointF((width - metrics.horizontalAdvance(line)) / 2, y), line, font, Qt::white, kInk,
			 0.12);
		y += metrics.descent() * 0.6;
	}
	return banner;
}

Plan planSpin(const Face &face, const QString &who, uint32_t seed)
{
	std::mt19937 random(seed);
	Plan plan;
	plan.forced = face.forced >= 0;
	plan.target = plan.forced ? face.forced
				  : std::uniform_int_distribution<int>(0, std::max(0, face.count() - 1))(random);
	plan.into = std::uniform_real_distribution<double>(0.18, 0.82)(random);
	plan.turns = std::uniform_int_distribution<int>(5, 7)(random);
	plan.seconds = std::uniform_real_distribution<double>(7.0, 9.0)(random);
	plan.who = who;
	plan.topic = plan.forced ? face.forcedText : face.labels[plan.target];
	const QColor colour = face.colours[plan.target];
	plan.banner = paintBanner(who, plan.topic, plan.forced, colour);

	// The slice it lands on, lit: drawn over the wheel, turning with it.
	const QPainterPath path = slicePath(face.starts[plan.target], face.starts[plan.target + 1], kRadius);
	const QRectF box = path.boundingRect().adjusted(-12, -12, 12, 12);
	QImage wedge(box.size().toSize() + QSize(1, 1), QImage::Format_ARGB32_Premultiplied);
	wedge.fill(Qt::transparent);
	QPainter painter(&wedge);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(-box.topLeft());
	painter.setBrush(QColor(255, 255, 255, 90));
	painter.setPen(QPen(Qt::white, 14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.drawPath(path);
	painter.end();
	plan.wedge = std::move(wedge);
	plan.wedgeAt = box.topLeft();
	return plan;
}

// ---- the spinner ----------------------------------------------------------------------------------------------------

void Spinner::show(std::shared_ptr<const Face> face)
{
	face_ = std::move(face);
	if (!spinning_)
		landed_ = false; // the lit slice and the reveal were of the old topics
	lastSlice_ = sliceUnderPointer();
}

void Spinner::start(std::shared_ptr<const Face> face, const Plan &plan)
{
	face_ = std::move(face);
	plan_ = plan;
	spinning_ = true;
	landed_ = false;
	t_ = 0;
	from_ = wrap(angle_);
	angle_ = from_;
	const double target = face_->starts[plan.target] + plan.into * (face_->starts[plan.target + 1] -
									 face_->starts[plan.target]);
	// The pointer is at 0 on screen, so the wheel stops turned to minus the target.
	total_ = kTau * plan.turns + wrap(-target - from_) + kPull;
	lastSlice_ = sliceUnderPointer();
	ticks_ = 0;
}

double Spinner::wheelAt(double t) const
{
	if (t < kPullSeconds)
		return from_ - kPull * std::sin(kPi / 2 * t / kPullSeconds);
	const double u = std::min(1.0, (t - kPullSeconds) / (plan_.seconds - kPullSeconds));
	return from_ - kPull + total_ * (1 - std::pow(1 - u, kEase));
}

int Spinner::sliceUnderPointer() const
{
	return face_ ? face_->sliceAt(-angle_) : -1;
}

bool Spinner::advance(double seconds)
{
	bool stopped = false;
	if (spinning_) {
		t_ += seconds;
		if (t_ >= plan_.seconds) {
			angle_ = wheelAt(plan_.seconds);
			spinning_ = false;
			landed_ = true;
			sinceLanded_ = 0;
			stopped = true;
		} else {
			angle_ = wheelAt(t_);
		}
		const int slice = sliceUnderPointer();
		if (slice != lastSlice_) {
			ticks_++;
			pointerSpeed_ -= 9; // a peg knocks the pointer's tip down
			lastSlice_ = slice;
		}
	} else {
		sinceLanded_ += seconds;
	}

	// The pointer springs back up between pegs.
	constexpr double kStiffness = 600, kDamping = 15;
	constexpr int kSteps = 4;
	for (int i = 0; i < kSteps; i++) {
		const double dt = seconds / kSteps;
		pointerSpeed_ += (-kStiffness * pointer_ - kDamping * pointerSpeed_) * dt;
		pointer_ += pointerSpeed_ * dt;
		if (pointer_ < -0.5 || pointer_ > 0.08) {
			pointer_ = std::clamp(pointer_, -0.5, 0.08);
			pointerSpeed_ = 0;
		}
	}
	return stopped;
}

double Spinner::bannerScale() const
{
	if (!landed_ || spinning_)
		return 0;
	const double t = sinceLanded_ - 0.25;
	if (t <= 0)
		return 0;
	const double u = std::min(1.0, t / 0.5);
	constexpr double c1 = 1.9, c3 = c1 + 1; // easeOutBack
	return 1 + c3 * std::pow(u - 1, 3) + c1 * std::pow(u - 1, 2);
}

double Spinner::wedgeOpacity() const
{
	if (!landed_ || spinning_)
		return 0;
	if (sinceLanded_ < 2.4)
		return 0.35 + 0.65 * std::abs(std::cos(kPi * sinceLanded_ * 2.5));
	return 0.75;
}

QImage compose(const Spinner &spinner, const QImage &hub, const QImage &pointer, double scale)
{
	const int side = static_cast<int>(kSize * scale);
	QImage picture(side, side, QImage::Format_ARGB32_Premultiplied);
	picture.fill(Qt::transparent);
	QPainter painter(&picture);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.scale(scale, scale);
	const auto &face = spinner.face();
	if (face) {
		painter.save();
		painter.translate(kCentre);
		painter.rotate(degrees(spinner.angle()));
		painter.drawImage(-kCentre, face->disc);
		if (const double lit = spinner.wedgeOpacity(); lit > 0) {
			painter.setOpacity(lit);
			painter.drawImage(spinner.plan().wedgeAt, spinner.plan().wedge);
		}
		painter.restore();
	}
	painter.drawImage(kCentre - QPointF(hub.width() / 2.0, hub.height() / 2.0), hub);
	painter.save();
	painter.translate(kPointerAt);
	painter.rotate(degrees(spinner.pointer()));
	painter.drawImage(-kPointerPivot, pointer);
	painter.restore();
	if (const double s = spinner.bannerScale(); s > 0) {
		const QImage &banner = spinner.plan().banner;
		painter.translate(kBannerAt);
		painter.scale(s, s);
		painter.drawImage(QPointF(-banner.width() / 2.0, -banner.height() / 2.0), banner);
	}
	return picture;
}

} // namespace sarxina::wheelofyap
