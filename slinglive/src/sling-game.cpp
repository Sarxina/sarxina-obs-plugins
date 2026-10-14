#include "sling-game.hpp"

#include <algorithm>
#include <cmath>

namespace sling {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPowerOffSeconds = 3.6;   // how long the panel stays off
constexpr double kReloadSeconds = 0.35;    // pause before a new head sits on the slingshot
constexpr double kFloatBackSeconds = 2.4;  // a knocked button floats back after this long
constexpr uint32_t kButtonHighlight = 0x4f5565;

bool inside(const Button &b, double x, double y, double pad = 0.0)
{
	return std::abs(x - b.x) <= kButtonW / 2 + pad && std::abs(y - b.y) <= kButtonH / 2 + pad;
}

} // namespace

Game::Game() : buttons_{{
		{ButtonId::Stream, 0.66, 0.44},
		{ButtonId::Exit, 0.87, 0.50},
		{ButtonId::Record, 0.60, 0.20},
		{ButtonId::VirtualCam, 0.84, 0.24},
		{ButtonId::Studio, 0.62, 0.68},
		{ButtonId::Settings, 0.85, 0.74},
	}},
	rng_(std::random_device{}())
{
	for (size_t i = 0; i < buttons_.size(); i++) {
		Button &b = buttons_[i];
		const int n = static_cast<int>(i);
		b.phase = n * 1.7;
		b.driftX = (n % 2 ? 1 : -1) * (40 + n * 9);
		b.driftY = (n % 3 - 1) * 34 + 22;
	}
	plinko_.fit(ground());
	place();
	reload();
}

double Game::random()
{
	return std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
}

void Game::setPractice(bool practice)
{
	if (practice_ == practice)
		return;
	practice_ = practice;
	panel_ = Panel{}; // practice starts with everything off; in real mode the dock fills this in from OBS
}

void Game::setHeight(double height)
{
	height_ = std::max(kMinHeight, height);
	plinko_.fit(ground());
	if (shot_.state == ShotState::Ready)
		reload();
}

Point Game::home(const Button &b) const
{
	if (mode_ == PlayMode::Plinko)
		return {plinko_.slotX(b.id), plinko_.slotY()};
	return {kWidth * b.fx, 34.0 + (ground() - 74.0) * b.fy};
}

double Game::buttonWidth() const
{
	return mode_ == PlayMode::Plinko ? plinko_.buttonWidth() : kButtonW;
}

double Game::power() const
{
	return std::sqrt(2.0 * kGravity * (ground() - kHeadRadius - 4.0)) / 73.0;
}

bool Game::overGear(double x, double y)
{
	return std::hypot(x - (kWidth - 16.0), y - 14.0) < 13.0;
}

void Game::place()
{
	for (Button &b : buttons_) {
		const Point h = home(b);
		b.x = h.x;
		b.y = h.y;
		b.state = ButtonState::Floating;
		b.angle = 0;
	}
}

void Game::reload()
{
	const Point f = fork();
	shot_.x = f.x;
	shot_.y = f.y;
	shot_.vx = shot_.vy = 0;
	shot_.state = ShotState::Ready;
	shot_.rotation = 0;
	if (mode_ == PlayMode::Plinko)
		plinko_.hang(shot_);
}

void Game::showToast(const char *key, double now)
{
	toast_ = key;
	toastAt_ = now;
}

void Game::burst(double x, double y, int count, uint32_t rgb)
{
	for (int i = 0; i < count; i++) {
		const double a = random() * 6.283;
		const double s = 80 + random() * 260;
		particles_.push_back({x, y, std::cos(a) * s, std::sin(a) * s - 80, 0.0, 0.35 + random() * 0.3, rgb});
	}
}

void Game::knock(Button &b, double vx, double vy)
{
	b.state = ButtonState::Knocked;
	b.vx = vx;
	b.vy = vy;
	b.spin = (random() - 0.5) * 14;
	b.knockedFor = 0;
}

const char *Game::label(ButtonId id, bool on)
{
	switch (id) {
	case ButtonId::Stream:
		return on ? "StopStreaming" : "StartStreaming";
	case ButtonId::Record:
		return on ? "StopRecording" : "StartRecording";
	case ButtonId::VirtualCam:
		return on ? "StopVirtualCam" : "StartVirtualCam";
	case ButtonId::Studio:
		return "StudioMode";
	case ButtonId::Settings:
		return "Settings";
	case ButtonId::Exit:
		break;
	}
	return "Exit";
}

bool Game::isOn(const Button &b) const
{
	switch (b.id) {
	case ButtonId::Stream:
		return panel_.streaming;
	case ButtonId::Record:
		return panel_.recording;
	case ButtonId::VirtualCam:
		return panel_.virtualCam;
	case ButtonId::Studio:
		return panel_.studioMode;
	default:
		return false;
	}
}

// What a hit does.
void Game::press(Button &b, double now)
{
	b.flash = 1;
	if (observer_)
		observer_(b.id);

	const bool real = !practice_ && host_;
	switch (b.id) {
	case ButtonId::Stream: {
		const bool on = !panel_.streaming;
		if (practice_) {
			panel_.streaming = on;
			panel_.liveSince = now;
		} else if (real) {
			host_->setStreaming(on);
		}
		showToast(on ? "Toast.StreamingStarted" : "Toast.StreamingStopped", now);
		break;
	}
	case ButtonId::Record: {
		const bool on = !panel_.recording;
		if (practice_) {
			panel_.recording = on;
			panel_.recordingSince = now;
		} else if (real) {
			host_->setRecording(on);
		}
		showToast(on ? "Toast.RecordingStarted" : "Toast.RecordingStopped", now);
		break;
	}
	case ButtonId::VirtualCam: {
		const bool on = !panel_.virtualCam;
		if (practice_)
			panel_.virtualCam = on;
		else if (real)
			host_->setVirtualCam(on);
		showToast(on ? "Toast.VirtualCamOn" : "Toast.VirtualCamOff", now);
		break;
	}
	case ButtonId::Studio: {
		const bool on = !panel_.studioMode;
		if (practice_)
			panel_.studioMode = on;
		else if (real)
			host_->setStudioMode(on);
		showToast(on ? "Toast.StudioModeOn" : "Toast.StudioModeOff", now);
		break;
	}
	case ButtonId::Settings:
		if (real)
			host_->openSettings();
		showToast("Toast.Settings", now);
		break;
	case ButtonId::Exit:
		powerOff(now);
		break;
	}
}

// The head coming down on a button, from the slingshot or through the Plinko board.
void Game::hit(Button &b, double now)
{
	press(b, now);
	burst(shot_.x, shot_.y, 14, 0xffd24a);
	shake_ = std::max(shake_, 0.5);
}

// Plinko: the head has reached the bottom, and the button in that slot is pressed.
void Game::land(double now)
{
	shot_.state = ShotState::Done;
	shot_.doneAt = now;
	for (Button &b : buttons_) {
		if (b.state == ButtonState::Floating && std::abs(b.x - shot_.x) < plinko_.slotWidth() / 2) {
			hit(b, now);
			break;
		}
	}
}

// The gag: everything falls over and the panel powers off like an old television. OBS itself stays open.
void Game::powerOff(double now)
{
	if (off_)
		return;
	off_ = true;
	offAt_ = now;
	shake_ = 1;
	if (panel_.streaming) {
		if (practice_)
			panel_.streaming = false;
		else if (host_)
			host_->setStreaming(false);
	}
	for (Button &b : buttons_) {
		if (b.state == ButtonState::Floating)
			knock(b, (random() - 0.5) * 500, -200 - random() * 300);
	}
}

void Game::setMode(PlayMode mode)
{
	mode_ = mode;
	place();
	reload();
	if (mode == PlayMode::FloatingButtons) {
		for (Button &b : buttons_) {
			b.vx = b.driftX * 1.6;
			b.vy = b.driftY * 1.6;
		}
	}
}

void Game::use(PlayMode mode, double now)
{
	setMode(mode);
	showToast(mode == PlayMode::Slingshot ? "Toast.Slingshot"
					      : mode == PlayMode::Plinko ? "Toast.Plinko" : "Toast.FloatingButtons",
		  now);
}

void Game::reset(double now)
{
	place();
	reload();
	if (practice_)
		panel_ = Panel{};
	showToast("Toast.Reset", now);
}

void Game::pointerDown(double x, double y, double now)
{
	mouse_ = {x, y};
	if (off_)
		return;
	if (mode_ == PlayMode::FloatingButtons) {
		for (Button &b : buttons_) {
			if (b.state == ButtonState::Floating && inside(b, x, y)) {
				press(b, now);
				burst(b.x, b.y, 10, 0xfefefe);
				break;
			}
		}
		return;
	}
	if (mode_ == PlayMode::Plinko) {
		plinko_.aim(shot_, x);
		if (shot_.state == ShotState::Ready)
			plinko_.drop(shot_);
		return;
	}
	if (shot_.state == ShotState::Ready && std::hypot(x - shot_.x, y - shot_.y) < 46)
		shot_.state = ShotState::Drag;
}

void Game::pointerMove(double x, double y)
{
	mouse_ = {x, y};
	if (mode_ == PlayMode::Plinko)
		plinko_.aim(shot_, x);
	if (shot_.state != ShotState::Drag)
		return;
	const Point f = fork();
	const double dx = x - f.x, dy = y - f.y;
	const double d = std::hypot(dx, dy);
	const double k = std::min(1.0, kMaxPull / (d > 0 ? d : 1.0));
	shot_.x = f.x + dx * k;
	shot_.y = std::min(ground() - kHeadRadius, f.y + dy * k);
}

void Game::pointerUp()
{
	if (shot_.state != ShotState::Drag)
		return;
	const Point f = fork();
	const double dx = f.x - shot_.x, dy = f.y - shot_.y;
	if (std::hypot(dx, dy) < 14) {
		reload();
		return;
	}
	shot_.vx = dx * power();
	shot_.vy = dy * power();
	shot_.state = ShotState::Fly;
	shot_.flying = 0;
}

// The world, one small step at a time.
void Game::step(double dt, double now)
{
	const double gy = ground();
	const double half = buttonWidth() / 2;

	if (off_ && now - offAt_ >= kPowerOffSeconds * 1000.0) {
		off_ = false;
		place();
		reload();
	}
	if (shot_.state == ShotState::Done && now - shot_.doneAt >= kReloadSeconds * 1000.0)
		reload();

	for (Button &b : buttons_) {
		b.flash = std::max(0.0, b.flash - dt * 3);
		if (b.state == ButtonState::Floating) {
			if (mode_ == PlayMode::FloatingButtons) {
				// Drifting like a screensaver; Start Streaming also runs from the cursor.
				if (b.vx == 0 && b.vy == 0) {
					b.vx = b.driftX * 1.6;
					b.vy = b.driftY * 1.6;
				}
				if (b.id == ButtonId::Stream) {
					const double dx = b.x - mouse_.x, dy = b.y - mouse_.y;
					const double d = std::hypot(dx, dy);
					if (d < 120 && d > 0) {
						b.vx += dx / d * 900 * dt;
						b.vy += dy / d * 900 * dt;
					}
					const double speed = std::hypot(b.vx, b.vy);
					if (speed > 260) {
						b.vx *= 260 / speed;
						b.vy *= 260 / speed;
					}
				}
				b.x += b.vx * dt;
				b.y += b.vy * dt;
				if (b.x < kButtonW / 2 + 4) {
					b.x = kButtonW / 2 + 4;
					b.vx = std::abs(b.vx);
				}
				if (b.x > kWidth - kButtonW / 2 - 4) {
					b.x = kWidth - kButtonW / 2 - 4;
					b.vx = -std::abs(b.vx);
				}
				if (b.y < kButtonH / 2 + 30) {
					b.y = kButtonH / 2 + 30;
					b.vy = std::abs(b.vy);
				}
				if (b.y > gy - kButtonH / 2 - 4) {
					b.y = gy - kButtonH / 2 - 4;
					b.vy = -std::abs(b.vy);
				}
			} else {
				const Point h = home(b);
				const double bob = mode_ == PlayMode::Plinko ? 0 : 1; // in a slot, a button keeps still
				b.x = h.x + std::sin(now / 900 + b.phase) * 5 * bob;
				b.y = h.y + std::sin(now / 620 + b.phase * 2) * 7 * bob;
			}
		} else {
			b.knockedFor += dt;
			b.vy += kGravity * dt;
			b.x += b.vx * dt;
			b.y += b.vy * dt;
			b.angle += b.spin * dt;
			if (b.y > gy - kButtonH / 2) {
				b.y = gy - kButtonH / 2;
				b.vy *= -0.34;
				b.vx *= 0.7;
				b.spin *= 0.5;
				if (std::abs(b.vy) < 40) {
					b.vy = 0;
					b.angle += (std::floor(b.angle / kPi + 0.5) * kPi - b.angle) * 0.3; // settle flat
				}
			}
			if (b.x < half) {
				b.x = half;
				b.vx = std::abs(b.vx) * 0.5;
			}
			if (b.x > kWidth - half) {
				b.x = kWidth - half;
				b.vx = -std::abs(b.vx) * 0.5;
			}
			if (b.knockedFor > kFloatBackSeconds && !off_) {
				const Point h = home(b);
				b.x = h.x;
				b.y = h.y;
				b.angle = 0;
				b.state = ButtonState::Floating;
				b.vx = b.vy = 0;
				burst(h.x, h.y, 8, kButtonHighlight);
			}
		}
	}

	if (mode_ == PlayMode::Plinko) {
		if (shot_.state == ShotState::Fly && plinko_.fall(shot_, dt))
			land(now);
	} else if (shot_.state == ShotState::Fly) {
		shot_.flying += dt;
		shot_.vy += kGravity * dt;
		shot_.x += shot_.vx * dt;
		shot_.y += shot_.vy * dt;
		shot_.rotation += shot_.vx * dt / kHeadRadius;
		for (Button &b : buttons_) {
			if (b.state != ButtonState::Floating || !inside(b, shot_.x, shot_.y, kHeadRadius))
				continue;
			knock(b, shot_.vx * 0.55, shot_.vy * 0.4 - 220);
			hit(b, now);
			shot_.vx *= 0.62; // the head keeps going, so one shot can press several buttons
			shot_.vy = shot_.vy * 0.5 - 60;
		}
		if (shot_.y > gy - kHeadRadius) {
			shot_.y = gy - kHeadRadius;
			shot_.vy *= -0.42;
			shot_.vx *= 0.72;
			if (std::abs(shot_.vy) < 60)
				shot_.vy = 0;
		}
		if (shot_.x > kWidth - kHeadRadius) {
			shot_.x = kWidth - kHeadRadius;
			shot_.vx = -std::abs(shot_.vx) * 0.5;
		}
		if (shot_.x < kHeadRadius) {
			shot_.x = kHeadRadius;
			shot_.vx = std::abs(shot_.vx) * 0.5;
		}
		if (shot_.flying > 3.2 || (std::abs(shot_.vx) < 12 && shot_.vy == 0)) {
			shot_.state = ShotState::Done;
			shot_.doneAt = now;
		}
	}

	for (size_t i = particles_.size(); i-- > 0;) {
		Particle &p = particles_[i];
		p.age += dt;
		p.vy += 900 * dt;
		p.x += p.vx * dt;
		p.y += p.vy * dt;
		if (p.age > p.life)
			particles_.erase(particles_.begin() + static_cast<std::ptrdiff_t>(i));
	}
	shake_ = std::max(0.0, shake_ - dt * 2.4);
}

} // namespace sling
