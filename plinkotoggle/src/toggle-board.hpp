// PlinkoToggle's board as viewers see it: the shared Plinko board (plinko.hpp) with one slot for each toggle of the
// model loaded in VTube Studio, in VTube Studio's order, each with the toggle's name on a button at its bottom. Chat
// drops balls from the top centre, each one the head PlinkoLive drops, with the chatter's name over it. The toggle a
// ball comes down on is switched, and its button flashes.
//
// Nothing here knows about OBS: the source feeds the board time and what to show, and puts the picture it paints on
// screen. It is laid out in units, the scale of PlinkoLive's panel, and painted at two pixels a unit. It grows with the
// toggles: each has a slot of the same width, and a wider board has more rows of pegs, so the picture, and with it the
// source in OBS, gets bigger with every toggle.
#pragma once

#include "plinko.hpp"
#include "vts-toggles.hpp"

#include <QFont>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <random>
#include <vector>

class QPainter;

namespace sarxina::plinkotoggle {

// What the board shows: the toggles, or, when there are none, why not. The hint says how chat drops a ball.
struct Content {
	std::vector<Toggle> toggles;
	QString status;
	QString hint;

	bool operator==(const Content &other) const
	{
		return toggles == other.toggles && status == other.status && hint == other.hint;
	}
	bool operator!=(const Content &other) const { return !(*this == other); }
};

// A ball down in a slot.
struct Landing {
	int slot = 0;
	Toggle toggle;
	QString who;
};

class ToggleBoard {
public:
	ToggleBoard();

	// New toggles start the board afresh, without the balls that were falling; a new status or hint only shows.
	void setContent(const Content &content);
	const Content &content() const { return content_; }
	bool canDrop() const { return !content_.toggles.empty(); }

	// Drops a ball for `who`, if there are toggles. A ball with the same seed on the same board falls the same way.
	void drop(const QString &who, uint32_t seed);
	void drop(const QString &who);

	// Moves time on. Every ball that came down in a slot meanwhile is added to `landed`.
	void advance(double seconds, std::vector<Landing> &landed);
	// Whether anything moves or fades, so that the picture changes.
	bool animating() const;
	int falling() const;

	// The picture is drawn in two parts: the still one, everything that does not move, and over it the sprites, each a
	// small picture of its own centred on a point (in pixels of the still picture). A source in OBS puts them on screen
	// as they are; paint() puts them together into one picture. Every picture is premultiplied ARGB.
	struct Sprite {
		enum class Kind {
			Head,   // head()
			Button, // button(slot), flashing
			Name,   // name(name)
		};
		Kind kind;
		QPointF centre;
		double rotation = 0; // radians, clockwise
		double opacity = 1;
		int slot = -1;
		QString name;
	};
	QSize pictureSize() const { return pictureSize_; }
	const QImage &still();
	// Changes whenever the still picture does, and with it the buttons.
	uint64_t stillVersion() const { return stillVersion_ + (stillDirty_ ? 1 : 0); }
	std::vector<Sprite> sprites() const;
	const QImage &head() const { return headScaled_; }
	QImage button(int slot) const;
	QImage name(const QString &who) const;
	void paint(QImage &picture);

	// The board's shape, for the self-test.
	const Plinko &plinko() const { return plinko_; }
	double width() const { return width_; }
	double height() const { return height_; }
	bool verticalLabels() const { return vertical_; }

private:
	struct Ball : Body {
		QString who;
		double rotation = 0;
		double still = 0; // seconds it has hardly moved
		std::mt19937 random;
		bool down = false;
		double downFor = 0; // seconds since it came down
	};

	void layOut();
	void step(double dt, std::vector<Landing> &landed);
	void paintStill();
	QRectF buttonRect(int slot) const; // in units
	void paintButton(QPainter &g, const QRectF &button, int slot, bool flash) const;
	QSizeF nameSize(const QString &who) const;

	Content content_;
	Plinko plinko_;
	double width_ = 0, height_ = 0; // in units
	QSize pictureSize_;
	double ground_ = 0;
	double buttonHeight_ = 0;
	bool vertical_ = false; // labels read upwards, in tall buttons
	std::vector<QString> labels_;
	std::vector<double> flash_; // per slot: 1 right after a ball came down in it, fading to 0

	std::vector<Ball> balls_;
	double pending_ = 0; // seconds not stepped yet
	std::mt19937 seeds_;

	QFont label_, hint_, status_, name_;
	QImage head_, headScaled_;
	QImage still_; // everything that does not move
	bool stillDirty_ = true;
	uint64_t stillVersion_ = 0;
};

} // namespace sarxina::plinkotoggle
