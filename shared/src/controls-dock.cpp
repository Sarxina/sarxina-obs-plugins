#include "controls-dock.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>

#include <QActionGroup>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sarxina {

ControlsDock::ControlsDock(std::unique_ptr<Game> game, Use use, QWidget *parent)
	: QWidget(parent),
	  use_(use),
	  game_(std::move(game)),
	  panel_(*game_)
{
	setFocusPolicy(Qt::ClickFocus);
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);
	fitPanel();

	if (use_ != Use::Dock)
		return;

	elapsed_.start();
	panel_.setPractice(savedPractice());
	panel_.setHost(&bridge_);
	readObs();

	timer_.setTimerType(Qt::PreciseTimer);
	connect(&timer_, &QTimer::timeout, this, &ControlsDock::tick);

	obs_frontend_add_event_callback(frontendEvent, this);
	listening_ = true;
}

ControlsDock::~ControlsDock()
{
	if (listening_)
		obs_frontend_remove_event_callback(frontendEvent, this);
}

// ---- OBS ------------------------------------------------------------------------------------------------------------

void ControlsDock::frontendEvent(enum obs_frontend_event event, void *self)
{
	static_cast<ControlsDock *>(self)->onFrontendEvent(event);
}

void ControlsDock::onFrontendEvent(enum obs_frontend_event event)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		timer_.stop();
		bridge_.shutDown();
		obs_frontend_remove_event_callback(frontendEvent, this);
		listening_ = false;
		return;
	}
	bridge_.handleEvent(event, now());
	if (!panel_.practice()) {
		bridge_.apply(panel_.state());
		update();
	}
}

void ControlsDock::readObs()
{
	if (panel_.practice())
		return;
	bridge_.refresh(now());
	bridge_.apply(panel_.state());
}

// ---- settings and the gear menu -------------------------------------------------------------------------------------

void ControlsDock::setPractice(bool practice)
{
	if (practice == panel_.practice())
		return;
	panel_.setPractice(practice);
	readObs();
	if (use_ == Use::Dock)
		savePractice(practice);
	update();
}

bool ControlsDock::savedPractice()
{
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!path || !os_file_exists(path))
		return false; // real mode unless the settings say otherwise
	OBSDataAutoRelease settings = obs_data_create_from_json_file_safe(path, "bak");
	return settings && std::strcmp(obs_data_get_string(settings, "mode"), "practice") == 0;
}

void ControlsDock::savePractice(bool practice)
{
	BPtr<char> folder(obs_module_config_path(""));
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!folder || !path)
		return;
	os_mkdirs(folder);
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "mode", practice ? "practice" : "real");
	obs_data_save_json_safe(settings, path, "tmp", "bak");
}

void ControlsDock::openMenu()
{
	QMenu menu(this);
	auto *modes = new QActionGroup(&menu);
	QAction *real = menu.addAction(text("Menu.Real"));
	real->setCheckable(true);
	real->setChecked(!panel_.practice());
	modes->addAction(real);
	QAction *practice = menu.addAction(text("Menu.Practice"));
	practice->setCheckable(true);
	practice->setChecked(panel_.practice());
	modes->addAction(practice);

	const QPoint corner = mapToGlobal(QPoint(qRound(left_ + kWidth * scale_), qRound(27 * scale_)));
	const QAction *picked = menu.exec(corner - QPoint(menu.sizeHint().width(), 0));
	if (picked != real && picked != practice)
		return;
	setPractice(picked == practice);
	emit modeChosen();
}

// ---- time -----------------------------------------------------------------------------------------------------------

double ControlsDock::now() const
{
	return use_ == Use::Dock ? elapsed_.nsecsElapsed() / 1e6 : testClock_;
}

void ControlsDock::tick()
{
	const double t = now();
	pending_ += std::min(0.05, (t - last_) / 1000.0);
	last_ = t;

	if (!panel_.practice()) {
		bridge_.follow(t);
		bridge_.apply(panel_.state());
	}
	while (pending_ > kStep) {
		panel_.step(kStep, t);
		pending_ -= kStep;
	}
	update();
}

void ControlsDock::advance(double seconds)
{
	for (long i = 0, steps = std::lround(seconds / kStep); i < steps; i++) {
		testClock_ += kStep * 1000.0;
		panel_.step(kStep, testClock_);
	}
}

void ControlsDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (use_ != Use::Dock || !listening_)
		return;
	last_ = now();
	pending_ = 0;
	readObs();
	const QScreen *display = screen();
	const double hertz = display && display->refreshRate() > 1 ? display->refreshRate() : 60.0;
	timer_.start(std::clamp(static_cast<int>(1000.0 / hertz), 4, 33));
}

void ControlsDock::hideEvent(QHideEvent *event)
{
	timer_.stop();
	QWidget::hideEvent(event);
}

// ---- size, mouse, keys ----------------------------------------------------------------------------------------------

void ControlsDock::fitPanel()
{
	// The panel is always kWidth units wide, and the dock's shape decides the height. A dock too wide for the
	// shortest panel the game fits on shows the whole panel at the dock's height, in the middle.
	if (width() <= 0 || height() <= 0)
		return;
	scale_ = std::min(width() / kWidth, height() / panel_.minHeight());
	left_ = (width() - kWidth * scale_) / 2;
	panel_.setHeight(height() / scale_);
}

void ControlsDock::setPanelSize(int width, int height)
{
	resize(width, height);
	fitPanel();
}

void ControlsDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	fitPanel();
}

Point ControlsDock::toPanel(const QPointF &position) const
{
	if (scale_ <= 0)
		return {};
	return {(position.x() - left_) / scale_, position.y() / scale_};
}

void ControlsDock::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	const Point p = toPanel(event->position());
	if (PanelPainter::overGear(p)) {
		openMenu();
		return;
	}
	panel_.pointerDown(p, now());
	update();
}

void ControlsDock::mouseMoveEvent(QMouseEvent *event)
{
	panel_.pointerMove(toPanel(event->position()));
}

void ControlsDock::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	panel_.pointerUp();
	update();
}

void ControlsDock::keyPressEvent(QKeyEvent *event)
{
	if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
		QWidget::keyPressEvent(event);
		return;
	}
	const double t = now();
	if (event->key() == Qt::Key_R)
		panel_.reset(t);
	else if (event->key() == Qt::Key_X)
		panel_.powerOff(t);
	else if (!game_->keyPressed(panel_, event->key(), t)) {
		QWidget::keyPressEvent(event);
		return;
	}
	update();
}

// ---- drawing --------------------------------------------------------------------------------------------------------

void ControlsDock::paintEvent(QPaintEvent *)
{
	QPainter g(this);
	painter_.paint(g, panel_, now(), scale_, left_);
}

QImage ControlsDock::snapshot(qreal pixelRatio)
{
	QImage image(size() * pixelRatio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(pixelRatio);
	image.fill(Qt::black);
	QPainter g(&image);
	painter_.paint(g, panel_, now(), scale_, left_);
	return image;
}

} // namespace sarxina
