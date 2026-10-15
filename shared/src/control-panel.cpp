#include "control-panel.hpp"

#include "game.hpp"

#include <algorithm>
#include <cmath>

namespace sarxina {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPowerOffSeconds = 3.6;  // how long the panel stays off
constexpr double kReloadSeconds = 0.35;   // pause before a new head is ready
constexpr double kFloatBackSeconds = 2.4; // a knocked button floats back after this long
constexpr uint32_t kSparkColor = 0xffd24a;
constexpr uint32_t kButtonHighlight = 0x4f5565;

} // namespace

ControlPanel::ControlPanel(Game &game) : game_(&game), rng_(std::random_device{}())
{
	for (size_t i = 0; i < buttons_.size(); i++)
		buttons_[i].control = static_cast<Control>(i);
	height_ = std::max(height_, minHeight());
	game.fit(*this);
	restart();
}

double ControlPanel::random()
{
	return std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
}

void ControlPanel::setPractice(bool practice)
{
	if (practice_ == practice)
		return;
	practice_ = practice;
	state_ = PanelState{}; // practice starts with everything off; in real mode the dock fills this in from OBS
}

double ControlPanel::minHeight() const
{
	return game_->minHeight();
}

void ControlPanel::setHeight(double height)
{
	height_ = std::max(minHeight(), height);
	game_->fit(*this);
	if (head_.state == HeadState::Ready)
		reload();
}

double ControlPanel::buttonWidth() const
{
	return game_->buttonWidth();
}

bool ControlPanel::touches(const Button &b, Point p, double pad) const
{
	return std::abs(p.x - b.x) <= buttonWidth() / 2 + pad && std::abs(p.y - b.y) <= kButtonH / 2 + pad;
}

const char *ControlPanel::label(const Button &b) const
{
	return isOn(b) ? info(b.control).labelOn : info(b.control).label;
}

void ControlPanel::place()
{
	for (Button &b : buttons_) {
		const Point h = game_->home(*this, b.control);
		b.x = h.x;
		b.y = h.y;
		b.knocked = false;
		b.angle = 0;
	}
}

void ControlPanel::restart()
{
	place();
	reload();
}

void ControlPanel::reload()
{
	head_ = Head{};
	game_->reload(*this);
}

void ControlPanel::showToast(const char *key, double now)
{
	toast_ = key;
	toastAt_ = now;
}

void ControlPanel::burst(Point at, int count, uint32_t rgb)
{
	for (int i = 0; i < count; i++) {
		const double a = random() * 6.283;
		const double s = 80 + random() * 260;
		Particle p;
		p.x = at.x;
		p.y = at.y;
		p.vx = std::cos(a) * s;
		p.vy = std::sin(a) * s - 80;
		p.life = 0.35 + random() * 0.3;
		p.rgb = rgb;
		particles_.push_back(p);
	}
}

void ControlPanel::knock(Button &b, double vx, double vy)
{
	b.knocked = true;
	b.vx = vx;
	b.vy = vy;
	b.spin = (random() - 0.5) * 14;
	b.knockedFor = 0;
}

void ControlPanel::press(Button &b, double now)
{
	b.flash = 1;
	if (observer_)
		observer_(b.control);
	if (b.control == Control::Exit) {
		powerOff(now);
		return;
	}

	const ControlInfo &control = info(b.control);
	const bool on = !control.toggles || !state_.isOn(b.control);
	if (practice_) {
		if (control.toggles)
			state_.set(b.control, on, now);
	} else if (host_) {
		host_->press(b.control, on);
	}
	showToast(on ? control.toastOn : control.toastOff, now);
}

void ControlPanel::hit(Button &b, double now)
{
	press(b, now);
	burst(head_.at(), 14, kSparkColor);
	shake_ = std::max(shake_, 0.5);
}

// The gag: everything falls over and the panel powers off like an old television. OBS itself stays open.
void ControlPanel::powerOff(double now)
{
	if (off_)
		return;
	off_ = true;
	offAt_ = now;
	shake_ = 1;
	if (state_.isOn(Control::Stream)) {
		if (practice_)
			state_.set(Control::Stream, false, now);
		else if (host_)
			host_->press(Control::Stream, false);
	}
	for (Button &b : buttons_) {
		if (!b.knocked)
			knock(b, (random() - 0.5) * 500, -200 - random() * 300);
	}
}

void ControlPanel::reset(double now)
{
	restart();
	if (practice_)
		state_ = PanelState{};
	showToast("Toast.Reset", now);
}

void ControlPanel::pointerDown(Point p, double now)
{
	if (!off_)
		game_->pointerDown(*this, p, now);
}

void ControlPanel::pointerMove(Point p)
{
	game_->pointerMove(*this, p);
}

void ControlPanel::pointerUp()
{
	game_->pointerUp(*this);
}

// A knocked-over button falls, bounces and tumbles, and after a while floats back home.
void ControlPanel::stepKnocked(Button &b, double dt)
{
	b.knockedFor += dt;
	b.fall(dt);
	b.angle += b.spin * dt;
	if (bounceOnFloor(b, ground() - kButtonH / 2, 0.34, 0.7, 40)) {
		b.spin *= 0.5;
		if (b.vy == 0)
			b.angle += (std::floor(b.angle / kPi + 0.5) * kPi - b.angle) * 0.3; // settle flat
	}
	const double half = buttonWidth() / 2;
	bounceBetween(b.x, b.vx, half, kWidth - half, 0.5);

	if (b.knockedFor > kFloatBackSeconds && !off_) {
		const Point h = game_->home(*this, b.control);
		b.x = h.x;
		b.y = h.y;
		b.angle = 0;
		b.knocked = false;
		b.vx = b.vy = 0;
		burst(h, 8, kButtonHighlight);
	}
}

// The world, one small step at a time.
void ControlPanel::step(double dt, double now)
{
	if (off_ && now - offAt_ >= kPowerOffSeconds * 1000.0) {
		off_ = false;
		restart();
	}
	if (head_.state == HeadState::Resting && now - head_.restingSince >= kReloadSeconds * 1000.0)
		reload();

	for (Button &b : buttons_) {
		b.flash = std::max(0.0, b.flash - dt * 3);
		if (b.knocked)
			stepKnocked(b, dt);
		else
			game_->moveButton(*this, b, dt, now);
	}
	game_->step(*this, dt, now);

	for (size_t i = particles_.size(); i-- > 0;) {
		Particle &p = particles_[i];
		p.age += dt;
		p.fall(dt, 900);
		if (p.age > p.life)
			particles_.erase(particles_.begin() + static_cast<std::ptrdiff_t>(i));
	}
	shake_ = std::max(0.0, shake_ - dt * 2.4);
}

} // namespace sarxina
