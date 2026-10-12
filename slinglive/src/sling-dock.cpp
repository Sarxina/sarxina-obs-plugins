#include "sling-dock.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>

#include <QActionGroup>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageReader>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QRawFont>
#include <QScreen>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace sling;

namespace {

// OBS's own dark theme
const QColor kPanelColor(0x27, 0x2a, 0x33);
const QColor kGroundColor(0x20, 0x23, 0x2b);
const QColor kStatusBarColor(0x16, 0x18, 0x1d);
const QColor kButtonColor(0x3c, 0x40, 0x4d);
const QColor kButtonOnColor(0x28, 0x4c, 0xb8);
const QColor kButtonEdgeColor(255, 255, 255, 20);
const QColor kTextColor(0xfe, 0xfe, 0xfe);
const QColor kFlashTextColor(0x1d, 0x1f, 0x26);
const QColor kDimColor(0x9a, 0xa0, 0xad);
const QColor kLiveColor(0xe0, 0x39, 0x3e);
const QColor kIdleColor(0x5a, 0x5f, 0x6b);
const QColor kWoodColor(0x8a, 0x5a, 0x2b);
const QColor kBandColor(0x3b, 0x24, 0x16);
const QColor kPlainHeadColor(0x5b, 0x7b, 0xd6);

constexpr double kHeadDrawRadius = kHeadRadius + 3; // the picture is drawn a little larger than the head that hits
constexpr int kHeadPictureSize = 256;               // a chosen picture is kept at this size
constexpr double kShadowBlur = 4.0;                 // device pixels
constexpr double kShadowDrop = 3.0;                 // device pixels
constexpr double kShadowOpacity = 0.45;
constexpr double kToastMs = 1900.0;
constexpr double kTextHairline = 0.3;               // device pixels

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

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

// Centre-crops a picture to a square and cuts it to a circle.
QImage cutToCircle(const QImage &picture, int maxSize)
{
	const int side = std::min(picture.width(), picture.height());
	QImage square = picture.copy((picture.width() - side) / 2, (picture.height() - side) / 2, side, side)
				.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	if (side > maxSize)
		square = square.scaled(maxSize, maxSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	QImage round(square.size(), QImage::Format_ARGB32_Premultiplied);
	round.fill(Qt::transparent);
	QPainter g(&round);
	g.setRenderHint(QPainter::Antialiasing);
	g.setPen(Qt::NoPen);
	g.setBrush(QBrush(square));
	g.drawEllipse(QRectF(0, 0, round.width(), round.height()));
	return round;
}

} // namespace

SlingDock::SlingDock(Use use, QWidget *parent) : QWidget(parent), use_(use)
{
	setFocusPolicy(Qt::ClickFocus);
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	const QStringList family{QStringLiteral("Segoe UI"), QStringLiteral("Open Sans")};
	label_.font = panelFont(family, 13);
	heading_.font = panelFont(family, 12, QFont::DemiBold);
	toast_.font = panelFont(family, 13, QFont::DemiBold);
	status_.font = panelFont(family, 11);
	gear_.font = panelFont({QStringLiteral("Segoe UI Symbol"), QStringLiteral("Segoe UI")}, 15);
	for (Face *face : {&label_, &heading_, &toast_, &status_, &gear_})
		face->baseline = baselineBelowMiddle(face->font);

	loadHead();
	fitPanel();

	if (use_ != Use::Dock)
		return;

	elapsed_.start();
	loadSettings();
	game_.setHost(&bridge_);
	if (!game_.practice()) {
		bridge_.refresh(now());
		bridge_.apply(game_.panel());
	}

	timer_.setTimerType(Qt::PreciseTimer);
	connect(&timer_, &QTimer::timeout, this, &SlingDock::tick);

	obs_frontend_add_event_callback(frontendEvent, this);
	listening_ = true;
}

SlingDock::~SlingDock()
{
	if (listening_)
		obs_frontend_remove_event_callback(frontendEvent, this);
}

// ---- OBS ------------------------------------------------------------------------------------------------------------

void SlingDock::frontendEvent(enum obs_frontend_event event, void *self)
{
	static_cast<SlingDock *>(self)->onFrontendEvent(event);
}

void SlingDock::onFrontendEvent(enum obs_frontend_event event)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		timer_.stop();
		bridge_.shutDown();
		obs_frontend_remove_event_callback(frontendEvent, this);
		listening_ = false;
		return;
	}
	bridge_.handleEvent(event, now());
	if (!game_.practice()) {
		bridge_.apply(game_.panel());
		update();
	}
}

void SlingDock::setPractice(bool practice)
{
	if (practice == game_.practice())
		return;
	game_.setPractice(practice);
	if (!practice) {
		bridge_.refresh(now());
		bridge_.apply(game_.panel());
	}
	saveSettings();
	update();
}

void SlingDock::loadSettings()
{
	bool practice = false; // real mode unless the settings say otherwise
	BPtr<char> path(obs_module_config_path("config.json"));
	if (path && os_file_exists(path)) {
		OBSDataAutoRelease settings = obs_data_create_from_json_file_safe(path, "bak");
		if (settings)
			practice = std::strcmp(obs_data_get_string(settings, "mode"), "practice") == 0;
	}
	game_.setPractice(practice);
}

void SlingDock::saveSettings() const
{
	if (use_ != Use::Dock)
		return;
	BPtr<char> folder(obs_module_config_path(""));
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!folder || !path)
		return;
	os_mkdirs(folder);
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "mode", game_.practice() ? "practice" : "real");
	obs_data_save_json_safe(settings, path, "tmp", "bak");
}

// ---- the head picture -----------------------------------------------------------------------------------------------

void SlingDock::setHead(const QImage &picture)
{
	head_ = picture.isNull() ? QImage() : cutToCircle(picture, kHeadPictureSize);
	headScaled_ = QImage();
	update();
}

void SlingDock::loadHead()
{
	customHead_ = false;
	if (use_ == Use::Dock) {
		BPtr<char> chosen(obs_module_config_path("head.png"));
		QImage picture;
		if (chosen && picture.load(QString::fromUtf8(chosen.Get()))) {
			customHead_ = true;
			setHead(picture);
			return;
		}
	}
	setHead(QImage(QStringLiteral(":/slinglive/head.png")));
}

void SlingDock::chooseHead()
{
	QStringList patterns;
	for (const QByteArray &format : QImageReader::supportedImageFormats())
		patterns << QStringLiteral("*.") + QString::fromLatin1(format);
	const QString filter = QStringLiteral("%1 (%2)").arg(text("Head.Pictures"), patterns.join(QLatin1Char(' ')));
	const QString file = QFileDialog::getOpenFileName(this, text("Head.Choose"), QString(), filter);
	if (file.isEmpty())
		return;

	QImageReader reader(file);
	reader.setAutoTransform(true);
	const QImage picture = reader.read();
	if (picture.isNull()) {
		QMessageBox::warning(this, text("DockTitle"), text("Head.CannotOpen").arg(QFileInfo(file).fileName()));
		return;
	}

	// Keep a small square copy with the plugin's settings, so the picture stays even if the file moves.
	const int side = std::min(picture.width(), picture.height());
	const QImage square = picture.copy((picture.width() - side) / 2, (picture.height() - side) / 2, side, side)
				      .scaled(kHeadPictureSize, kHeadPictureSize, Qt::IgnoreAspectRatio,
					      Qt::SmoothTransformation);
	BPtr<char> folder(obs_module_config_path(""));
	BPtr<char> path(obs_module_config_path("head.png"));
	if (folder && path) {
		os_mkdirs(folder);
		square.save(QString::fromUtf8(path.Get()), "PNG");
	}
	customHead_ = true;
	setHead(square);
}

void SlingDock::resetHead()
{
	BPtr<char> path(obs_module_config_path("head.png"));
	if (path)
		QFile::remove(QString::fromUtf8(path.Get()));
	loadHead();
}

const QImage &SlingDock::headAt(int devicePixels)
{
	const int size = std::max(8, devicePixels);
	if (headScaled_.width() != size)
		headScaled_ = head_.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	return headScaled_;
}

// ---- the gear -------------------------------------------------------------------------------------------------------

void SlingDock::openMenu()
{
	QMenu menu(this);
	auto *modes = new QActionGroup(&menu);
	QAction *real = menu.addAction(text("Menu.Real"));
	real->setCheckable(true);
	real->setChecked(!game_.practice());
	modes->addAction(real);
	QAction *practice = menu.addAction(text("Menu.Practice"));
	practice->setCheckable(true);
	practice->setChecked(game_.practice());
	modes->addAction(practice);
	menu.addSeparator();
	QAction *choose = menu.addAction(text("Menu.ChooseHead"));
	QAction *reset = menu.addAction(text("Menu.ResetHead"));
	reset->setEnabled(customHead_);

	const double scale = width() / kWidth;
	const QPoint corner = mapToGlobal(QPoint(width(), qRound(27 * scale)));
	const QAction *picked = menu.exec(corner - QPoint(menu.sizeHint().width(), 0));
	if (picked == real)
		setPractice(false);
	else if (picked == practice)
		setPractice(true);
	else if (picked == choose)
		chooseHead();
	else if (picked == reset)
		resetHead();
}

// ---- time -----------------------------------------------------------------------------------------------------------

double SlingDock::now() const
{
	return use_ == Use::Dock ? elapsed_.nsecsElapsed() / 1e6 : testClock_;
}

void SlingDock::tick()
{
	const double t = now();
	pending_ += std::min(0.05, (t - last_) / 1000.0);
	last_ = t;

	if (!game_.practice()) {
		bridge_.follow(t);
		bridge_.apply(game_.panel());
	}
	while (pending_ > kStep) {
		game_.step(kStep, t);
		pending_ -= kStep;
	}
	update();
}

void SlingDock::advance(double seconds)
{
	for (long i = 0, steps = std::lround(seconds / kStep); i < steps; i++) {
		testClock_ += kStep * 1000.0;
		game_.step(kStep, testClock_);
	}
}

void SlingDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (use_ != Use::Dock || !listening_)
		return;
	last_ = now();
	pending_ = 0;
	if (!game_.practice()) {
		bridge_.refresh(last_);
		bridge_.apply(game_.panel());
	}
	const QScreen *display = screen();
	const double hertz = display && display->refreshRate() > 1 ? display->refreshRate() : 60.0;
	timer_.start(std::clamp(static_cast<int>(1000.0 / hertz), 4, 33));
}

void SlingDock::hideEvent(QHideEvent *event)
{
	timer_.stop();
	QWidget::hideEvent(event);
}

// ---- size, mouse, keys ----------------------------------------------------------------------------------------------

void SlingDock::fitPanel()
{
	// The dock's width is always kWidth units; its shape decides the height.
	if (width() > 0)
		game_.setHeight(height() * kWidth / width());
}

void SlingDock::setPanelSize(int width, int height)
{
	resize(width, height);
	fitPanel();
}

void SlingDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	fitPanel();
}

QPointF SlingDock::toPanel(const QPointF &position) const
{
	return width() > 0 ? position * (kWidth / width()) : QPointF();
}

void SlingDock::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	const QPointF p = toPanel(event->position());
	if (Game::overGear(p.x(), p.y())) {
		openMenu();
		return;
	}
	game_.pointerDown(p.x(), p.y(), now());
	update();
}

void SlingDock::mouseMoveEvent(QMouseEvent *event)
{
	const QPointF p = toPanel(event->position());
	game_.pointerMove(p.x(), p.y());
}

void SlingDock::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	game_.pointerUp();
	update();
}

void SlingDock::keyPressEvent(QKeyEvent *event)
{
	if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
		QWidget::keyPressEvent(event);
		return;
	}
	const double t = now();
	switch (event->key()) {
	case Qt::Key_1:
		game_.useFloatingButtons(t);
		break;
	case Qt::Key_2:
		game_.useSlingshot(t);
		break;
	case Qt::Key_R:
		game_.reset(t);
		break;
	case Qt::Key_X:
		game_.powerOff(t);
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	update();
}

// ---- drawing --------------------------------------------------------------------------------------------------------

void SlingDock::paintEvent(QPaintEvent *)
{
	QPainter g(this);
	paintPanel(g, now());
}

QImage SlingDock::snapshot(qreal pixelRatio)
{
	QImage image(size() * pixelRatio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(pixelRatio);
	image.fill(Qt::black);
	QPainter g(&image);
	paintPanel(g, now());
	return image;
}

const QImage &SlingDock::buttonShadow(double deviceScale)
{
	if (!shadow_.isNull() && std::abs(shadowScale_ - deviceScale) < 1e-6)
		return shadow_;

	// The button's shape in device pixels, with room around it for the blur.
	const int margin = static_cast<int>(std::ceil(kShadowBlur * 3)) + 2;
	const int width = static_cast<int>(std::ceil(kButtonW * deviceScale)) + margin * 2;
	const int height = static_cast<int>(std::ceil(kButtonH * deviceScale)) + margin * 2;
	QImage shape(width, height, QImage::Format_ARGB32_Premultiplied);
	shape.fill(Qt::transparent);
	{
		QPainter g(&shape);
		g.setRenderHint(QPainter::Antialiasing);
		g.setPen(Qt::NoPen);
		g.setBrush(Qt::black);
		g.drawRoundedRect(QRectF((width - kButtonW * deviceScale) / 2, (height - kButtonH * deviceScale) / 2,
					 kButtonW * deviceScale, kButtonH * deviceScale),
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
	return shadow_;
}

void SlingDock::paintButton(QPainter &g, const Button &button, double deviceScale)
{
	// The shadow falls straight down on the screen, however the button is turned.
	const QImage &shadow = buttonShadow(deviceScale);
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
	g.setBrush(flash ? QColor(Qt::white) : game_.isOn(button) ? kButtonOnColor : kButtonColor);
	g.drawRoundedRect(QRectF(-kButtonW / 2, -kButtonH / 2, kButtonW, kButtonH), 4, 4);
	drawText(g, text(game_.label(button)), label_.font, label_.baseline, 0, 1, Qt::AlignHCenter,
		 flash ? kFlashTextColor : kTextColor);
	g.restore();
}

void SlingDock::paintPanel(QPainter &g, double now)
{
	const double W = kWidth, H = game_.height();
	const double scale = width() / W;
	const double deviceScale = scale * g.device()->devicePixelRatioF();
	if (scale <= 0)
		return;

	g.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	g.scale(scale, scale);
	g.fillRect(QRectF(0, 0, W, H), kPanelColor);

	g.save(); // everything from here to the matching restore shakes on a hit
	if (const double shake = game_.shake() * 7; shake > 0) {
		QRandomGenerator *random = QRandomGenerator::global();
		g.translate((random->generateDouble() - 0.5) * shake, (random->generateDouble() - 0.5) * shake);
	}

	drawText(g, text("Controls"), heading_.font, heading_.baseline, 10, 14, Qt::AlignLeft, kDimColor);
	drawText(g, QString(QChar(0x2699)), gear_.font, gear_.baseline, W - 16, 15, Qt::AlignHCenter, kDimColor);

	const double ground = game_.ground();
	g.fillRect(QRectF(0, ground, W, H - ground), kGroundColor);

	const Point fork = game_.fork();
	const Shot &shot = game_.shot();
	const bool slingshot = game_.mode() == PlayMode::Slingshot;
	const bool held = shot.state == ShotState::Drag || shot.state == ShotState::Ready;
	const QPen band(kBandColor, 4, Qt::SolidLine, Qt::RoundCap);

	if (slingshot) {
		g.setPen(QPen(kWoodColor, 9, Qt::SolidLine, Qt::RoundCap));
		g.drawLine(QPointF(fork.x, ground), QPointF(fork.x, fork.y + 34));
		g.drawLine(QPointF(fork.x, fork.y + 34), QPointF(fork.x - 15, fork.y - 6));
		g.drawLine(QPointF(fork.x, fork.y + 34), QPointF(fork.x + 15, fork.y - 6));
		// The far half of the band goes behind the head.
		g.setPen(band);
		g.drawLine(QPointF(fork.x + 15, fork.y - 6),
			   held ? QPointF(shot.x, shot.y) : QPointF(fork.x - 15, fork.y - 6));

		if (shot.state == ShotState::Drag) { // where it will go
			double x = shot.x, y = shot.y;
			double vx = (fork.x - shot.x) * game_.power(), vy = (fork.y - shot.y) * game_.power();
			g.setPen(Qt::NoPen);
			g.setBrush(QColor(255, 255, 255, 140));
			for (int i = 0; i < 26; i++) {
				for (int k = 0; k < 4; k++) {
					vy += kGravity * kStep;
					x += vx * kStep;
					y += vy * kStep;
				}
				if (y > ground)
					break;
				const double radius = 2.2 - i * 0.05;
				g.drawEllipse(QPointF(x, y), radius, radius);
			}
		}
	}

	for (const Button &button : game_.buttons())
		paintButton(g, button, deviceScale);

	if (slingshot && shot.state != ShotState::Done) {
		g.save();
		g.translate(shot.x, shot.y);
		g.rotate(degrees(shot.rotation));
		if (!head_.isNull()) {
			const QImage &head = headAt(static_cast<int>(std::ceil(kHeadDrawRadius * 2 * deviceScale)));
			g.drawImage(QRectF(-kHeadDrawRadius, -kHeadDrawRadius, kHeadDrawRadius * 2, kHeadDrawRadius * 2), head);
		} else {
			g.setPen(Qt::NoPen);
			g.setBrush(kPlainHeadColor);
			g.drawEllipse(QPointF(0, 0), kHeadRadius, kHeadRadius);
		}
		g.restore();
		if (held) { // the near half of the band, in front of the head
			g.setPen(band);
			g.drawLine(QPointF(fork.x - 15, fork.y - 6), QPointF(shot.x, shot.y));
		}
	}

	for (const Particle &particle : game_.particles()) {
		g.setOpacity(std::clamp(1.0 - particle.age / particle.life, 0.0, 1.0));
		g.fillRect(QRectF(particle.x - 2, particle.y - 2, 4, 4), QColor(QRgb(particle.rgb)));
	}
	g.setOpacity(1);

	if (const char *toast = game_.toast(); toast && now - game_.toastAt() < kToastMs) {
		const QString message = text(toast);
		const double width = QFontMetricsF(toast_.font).horizontalAdvance(message) + 22;
		g.setOpacity(std::clamp((kToastMs - (now - game_.toastAt())) / 300.0, 0.0, 1.0));
		g.setPen(Qt::NoPen);
		g.setBrush(QColor(0, 0, 0, 153));
		g.drawRoundedRect(QRectF(W / 2 - width / 2, 28, width, 24), 12, 12);
		drawText(g, message, toast_.font, toast_.baseline, W / 2, 41, Qt::AlignHCenter, Qt::white);
		g.setOpacity(1);
	}
	g.restore();

	// The status strip, like the bar along the bottom of OBS.
	g.fillRect(QRectF(0, H - 20, W, 20), kStatusBarColor);
	double x = 10;
	const auto light = [&](const QColor &color, const QString &words) {
		g.setPen(Qt::NoPen);
		g.setBrush(color);
		g.drawEllipse(QPointF(x + 4, H - 10), 4, 4);
		drawText(g, words, status_.font, status_.baseline, x + 13, H - 9, Qt::AlignLeft, kTextColor);
		x += 22 + QFontMetricsF(status_.font).horizontalAdvance(words);
	};
	const Panel &panel = game_.panel();
	const bool live = panel.streaming && !panel.connecting;
	light(live ? kLiveColor : kIdleColor,
	      live ? text("Status.Live") + QLatin1Char(' ') + clockText(now - panel.liveSince)
		   : panel.streaming ? text("Status.Connecting") : text("Status.Offline"));
	light(panel.recording ? kLiveColor : kIdleColor,
	      panel.recording ? text("Status.Recording") + QLatin1Char(' ') + clockText(now - panel.recordingSince)
			      : text("Status.NotRecording"));
	QString hint = text(slingshot ? "Hint.Slingshot" : "Hint.FloatingButtons");
	if (game_.practice())
		hint += QStringLiteral("   ") + text("Hint.Practice");
	drawText(g, hint, status_.font, status_.baseline, W - 10, H - 9, Qt::AlignRight, kDimColor);

	paintPowerOff(g, now);
}

// The panel powering off like an old television: two bars close to a white line, the line shrinks to a dot, the dot
// fades, and after a while the picture comes back.
void SlingDock::paintPowerOff(QPainter &g, double now)
{
	if (!game_.poweredOff())
		return;
	const double W = kWidth, H = game_.height();
	const double t = (now - game_.poweredOffAt()) / 1000.0;
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
