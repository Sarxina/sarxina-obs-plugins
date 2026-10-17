#include "toggle-board.hpp"

#include "panel-colors.hpp"
#include "plinko-painter.hpp"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <functional>

namespace sarxina::plinkotoggle {

namespace {

constexpr double kScale = 2.0;      // pixels of the picture per unit
constexpr double kSlotWidth = 56.0; // a toggle's slot: room for the head (30 across) and its name written upwards
constexpr int kLeastSlots = 5;      // the board is never narrower than this many slots
// The pegs about as PlinkoLive has them, two thirds of PlinkoLive's slot apart and 32 between rows. The bottom row
// reaches from the middle of the first slot to the middle of the last, so a wider board has more rows, and the
// spacing is stretched a little to make it reach.
constexpr double kPegSpacing = 640.0 / 6 * 2 / 3;
constexpr double kRowGap = 32.0;
constexpr double kWallRoom = 40.0; // from a wall to the nearest peg: the head (30 across) and the peg (4 round) fit
constexpr Point kStart{0, 48.0}; // x is the middle of the board
constexpr double kTopRowY = 76.0;
constexpr double kHeadRadius = 15.0;
constexpr double kHeadDrawRadius = 18.0; // the picture is drawn a little larger than the head that hits
constexpr double kSlotInset = 4.0; // between a button and the sides and the floor of its slot
// The dividers start a head's width below the bottom row of pegs: in narrow slots, a divider's top and a peg would
// otherwise hold the head between them.
constexpr double kDividerGap = 2 * kHeadRadius + 6;
constexpr double kSlotRoom = kDividerGap + 24; // from the bottom row of pegs down to the buttons, at the least
constexpr double kStrip = 24.0;          // along the bottom, under the buttons, with the hint
constexpr double kButtonHeight = 28.0;   // with the names written across
constexpr double kTallestButton = 160.0; // with the names written upwards
constexpr double kLabelMargin = 8.0;
constexpr double kStatusRoom = 72.0; // for why there are no toggles, where the buttons would be
constexpr double kCorner = 10.0;
constexpr double kNameHeight = 20.0; // a name tag
constexpr double kRestSeconds = 1.2; // a ball stays in its slot this long, then fades
constexpr double kFadeSeconds = 0.4;
constexpr double kMostStep = 0.25; // seconds; after a longer stall, time picks up from there

QFont font(int pixels, QFont::Weight weight = QFont::Normal)
{
	QFont f;
	f.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Open Sans")});
	f.setPixelSize(pixels);
	f.setWeight(weight);
	f.setHintingPreference(QFont::PreferNoHinting); // drawn scaled, so from the outlines
	return f;
}

double degrees(double radians)
{
	return radians * 180.0 / 3.14159265358979323846;
}

PlinkoShape shapeFor(double width, int count)
{
	PlinkoShape shape;
	shape.width = width;
	shape.slotCount = std::max(1, count);
	shape.start = {width / 2, kStart.y};
	shape.topRowY = kTopRowY;
	// Between the middles of the end slots, or on a board of one or two slots, two pegs of a full one wide. The end pegs
	// stay far enough from the walls for the head to fall between, or it would sit on a peg against the wall.
	const double span = std::max(width - width / shape.slotCount, width - 2 * kSlotWidth);
	const double reach = std::min(span, width - 2 * kWallRoom);
	shape.rows = 1 + std::max(1, static_cast<int>(std::lround(reach / kPegSpacing)));
	shape.pegSpacing = reach / (shape.rows - 1);
	shape.rowGap = kRowGap;
	shape.dividerGap = kDividerGap;
	shape.ballRadius = kHeadRadius;
	return shape;
}

} // namespace

ToggleBoard::ToggleBoard()
	: plinko_(PlinkoShape{}),
	  seeds_(std::random_device{}()),
	  label_(font(13)),
	  hint_(font(12, QFont::DemiBold)),
	  status_(font(15)),
	  name_(font(12, QFont::DemiBold)),
	  head_(QStringLiteral(":/plinkotoggle/head.png"))
{
	layOut();
}

void ToggleBoard::setContent(const Content &content)
{
	if (content == content_)
		return;
	const bool newToggles = !(content.toggles == content_.toggles);
	content_ = content;
	if (newToggles) {
		balls_.clear();
		layOut();
	}
	stillDirty_ = true;
}

// The board grows with the toggles: a slot each, at least kLeastSlots wide, with as many rows of pegs as its width
// takes, and as tall as the triangle, the slots and the longest name need.
void ToggleBoard::layOut()
{
	const int count = static_cast<int>(content_.toggles.size());
	width_ = std::max(count, kLeastSlots) * kSlotWidth;
	plinko_ = Plinko(shapeFor(width_, count));

	// The names go across the buttons when every one fits, otherwise upwards in taller buttons.
	const QFontMetricsF metrics(label_);
	const double across = plinko_.slotWidth() - 2 * kSlotInset - 2 * kLabelMargin;
	double widest = 0;
	for (const Toggle &toggle : content_.toggles)
		widest = std::max(widest, metrics.horizontalAdvance(toggle.name));
	vertical_ = widest > across;
	buttonHeight_ = vertical_ ? std::clamp(widest + 2 * kLabelMargin, 2 * kButtonHeight, kTallestButton) : kButtonHeight;
	ground_ = std::ceil(plinko_.bottomRowY() + kSlotRoom + (count ? buttonHeight_ : kStatusRoom) + kSlotInset);
	height_ = ground_ + kStrip;
	plinko_.setFloor(ground_, ground_ - kSlotInset - buttonHeight_ - kHeadRadius);
	pictureSize_ = QSize(static_cast<int>(std::ceil(width_ * kScale)), static_cast<int>(std::ceil(height_ * kScale)));

	const double length = vertical_ ? buttonHeight_ - 2 * kLabelMargin : across;
	labels_.clear();
	for (const Toggle &toggle : content_.toggles)
		labels_.push_back(metrics.elidedText(toggle.name, Qt::ElideRight, length));
	flash_.assign(static_cast<size_t>(count), 0.0);

	const int pixels = std::max(8, static_cast<int>(std::ceil(kHeadDrawRadius * 2 * kScale)));
	headScaled_ = head_.scaled(pixels, pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void ToggleBoard::drop(const QString &who)
{
	drop(who, seeds_());
}

void ToggleBoard::drop(const QString &who, uint32_t seed)
{
	if (!canDrop())
		return;
	Ball ball;
	ball.x = plinko_.shape().start.x;
	ball.y = plinko_.shape().start.y;
	ball.who = who;
	ball.random.seed(seed);
	balls_.push_back(std::move(ball));
}

int ToggleBoard::falling() const
{
	return static_cast<int>(std::count_if(balls_.begin(), balls_.end(), [](const Ball &b) { return !b.down; }));
}

bool ToggleBoard::animating() const
{
	return !balls_.empty() || std::any_of(flash_.begin(), flash_.end(), [](double f) { return f > 0; });
}

void ToggleBoard::advance(double seconds, std::vector<Landing> &landed)
{
	pending_ += std::clamp(seconds, 0.0, kMostStep);
	while (pending_ >= kStep) {
		pending_ -= kStep;
		step(kStep, landed);
	}
}

void ToggleBoard::step(double dt, std::vector<Landing> &landed)
{
	for (Ball &ball : balls_) {
		if (ball.down) {
			ball.downFor += dt;
			continue;
		}
		ball.fall(dt);
		ball.rotation += ball.vx * dt / kHeadRadius;
		if (!plinko_.bounce(ball, ball.still, dt, ball.random))
			continue;
		ball.down = true;
		const int slot = plinko_.slotAt(ball.x);
		if (slot < static_cast<int>(content_.toggles.size())) {
			flash_[static_cast<size_t>(slot)] = 1;
			landed.push_back({slot, content_.toggles[static_cast<size_t>(slot)], ball.who});
		}
	}
	balls_.erase(std::remove_if(balls_.begin(), balls_.end(),
				    [](const Ball &b) { return b.down && b.downFor >= kRestSeconds + kFadeSeconds; }),
		     balls_.end());
	for (double &flash : flash_)
		flash = std::max(0.0, flash - dt * 3);
}

// ---- painting -------------------------------------------------------------------------------------------------------

namespace {

// A picture of its own, `size` units big at two pixels a unit with a pixel to spare around it, painted by `paint` with
// the middle of the picture as the origin.
QImage picture(QSizeF size, const std::function<void(QPainter &)> &paint)
{
	QImage image(static_cast<int>(std::ceil(size.width() * kScale)) + 2,
		     static_cast<int>(std::ceil(size.height() * kScale)) + 2, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter g(&image);
	g.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
	g.translate(image.width() / 2.0, image.height() / 2.0);
	g.scale(kScale, kScale);
	paint(g);
	return image;
}

QRectF around(QSizeF size)
{
	return {-size.width() / 2, -size.height() / 2, size.width(), size.height()};
}

} // namespace

QRectF ToggleBoard::buttonRect(int slot) const
{
	const double slotWidth = plinko_.slotWidth();
	return {slotWidth * slot + kSlotInset, ground_ - kSlotInset - buttonHeight_, slotWidth - 2 * kSlotInset,
		buttonHeight_};
}

void ToggleBoard::paintButton(QPainter &g, const QRectF &button, int slot, bool flash) const
{
	g.setPen(QPen(kButtonEdgeColor, 1));
	g.setBrush(flash ? QColor(Qt::white) : kButtonColor);
	g.drawRoundedRect(button, 4, 4);

	g.setPen(flash ? kFlashTextColor : kTextColor);
	g.setFont(label_);
	const QString &label = labels_[static_cast<size_t>(slot)];
	if (!vertical_) {
		g.drawText(button, Qt::AlignCenter, label);
		return;
	}
	g.save();
	g.translate(button.center());
	g.rotate(-90); // read from the bottom up
	g.drawText(QRectF(-button.height() / 2, -button.width() / 2, button.height(), button.width()), Qt::AlignCenter,
		   label);
	g.restore();
}

QImage ToggleBoard::button(int slot) const
{
	const QSizeF size = buttonRect(slot).size();
	return picture(size, [&](QPainter &g) { paintButton(g, around(size), slot, true); });
}

QSizeF ToggleBoard::nameSize(const QString &who) const
{
	return {QFontMetricsF(name_).horizontalAdvance(who) + 16, kNameHeight};
}

// The chatter's name, on a dark tag that goes over their ball.
QImage ToggleBoard::name(const QString &who) const
{
	const QSizeF size = nameSize(who);
	return picture(size, [&](QPainter &g) {
		g.setPen(Qt::NoPen);
		g.setBrush(QColor(0, 0, 0, 170));
		g.drawRoundedRect(around(size), size.height() / 2, size.height() / 2);
		g.setPen(Qt::white);
		g.setFont(name_);
		g.drawText(around(size), Qt::AlignCenter, who);
	});
}

std::vector<ToggleBoard::Sprite> ToggleBoard::sprites() const
{
	const auto pixels = [](double x, double y) { return QPointF(x * kScale, y * kScale); };
	std::vector<Sprite> sprites;
	for (int slot = 0; slot < static_cast<int>(flash_.size()); slot++) {
		if (flash_[static_cast<size_t>(slot)] > 0) {
			const QPointF middle = buttonRect(slot).center();
			sprites.push_back({Sprite::Kind::Button, pixels(middle.x(), middle.y()), 0, 1, slot, {}});
		}
	}

	// A head waits at the top whenever the last one has fallen clear of it.
	const Point start = plinko_.shape().start;
	const bool clear = std::none_of(balls_.begin(), balls_.end(), [&start](const Ball &b) {
		return b.y < start.y + 2 * kHeadDrawRadius + 8;
	});
	if (canDrop() && clear)
		sprites.push_back({Sprite::Kind::Head, pixels(start.x, start.y), 0, 1, -1, {}});

	for (const Ball &ball : balls_) {
		const double fade = ball.down ? std::clamp(1.0 - (ball.downFor - kRestSeconds) / kFadeSeconds, 0.0, 1.0) : 1.0;
		sprites.push_back({Sprite::Kind::Head, pixels(ball.x, ball.y), ball.rotation, fade, -1, {}});
		if (ball.who.isEmpty())
			continue;
		const double width = nameSize(ball.who).width();
		const double x = std::clamp(ball.x, width / 2 + 4, std::max(width / 2 + 4, width_ - width / 2 - 4));
		sprites.push_back({Sprite::Kind::Name, pixels(x, ball.y - kHeadDrawRadius - 4 - kNameHeight / 2), 0, fade, -1,
				   ball.who});
	}
	return sprites;
}

// The board without what moves: the panel, the pegs and the buttons, or why there are none, and the hint along the
// bottom.
void ToggleBoard::paintStill()
{
	still_ = QImage(pictureSize_, QImage::Format_ARGB32_Premultiplied);
	still_.fill(Qt::transparent);
	QPainter g(&still_);
	g.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
	g.scale(kScale, kScale);

	QPainterPath panel;
	panel.addRoundedRect(QRectF(0, 0, width_, height_), kCorner, kCorner);
	g.fillPath(panel, kPanelColor);
	g.save();
	g.setClipPath(panel);
	g.fillRect(QRectF(0, ground_, width_, height_ - ground_), kGroundColor);
	g.restore();

	g.setPen(kDimColor);
	g.setFont(hint_);
	const QRectF strip(10, ground_, width_ - 20, height_ - ground_);
	g.drawText(strip, Qt::AlignLeft | Qt::AlignVCenter,
		   QFontMetricsF(hint_).elidedText(content_.hint, Qt::ElideRight, strip.width()));

	paintPlinko(g, plinko_, kIdleColor);
	for (int slot = 0; slot < static_cast<int>(labels_.size()); slot++)
		paintButton(g, buttonRect(slot), slot, false);

	if (!content_.status.isEmpty()) {
		g.setPen(kTextColor);
		g.setFont(status_);
		const double top = plinko_.bottomRowY() + kSlotRoom;
		g.drawText(QRectF(16, top, width_ - 32, ground_ - top), Qt::AlignCenter | Qt::TextWordWrap, content_.status);
	}
	stillDirty_ = false;
	stillVersion_++;
}

const QImage &ToggleBoard::still()
{
	if (stillDirty_)
		paintStill();
	return still_;
}

void ToggleBoard::paint(QImage &image)
{
	image = still().copy();
	QPainter g(&image);
	g.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	for (const Sprite &sprite : sprites()) {
		const QImage drawn = sprite.kind == Sprite::Kind::Head     ? headScaled_
				     : sprite.kind == Sprite::Kind::Button ? button(sprite.slot)
									   : name(sprite.name);
		g.save();
		g.setOpacity(sprite.opacity);
		g.translate(sprite.centre);
		g.rotate(degrees(sprite.rotation));
		g.drawImage(QPointF(-drawn.width() / 2.0, -drawn.height() / 2.0), drawn);
		g.restore();
	}
}

} // namespace sarxina::plinkotoggle
