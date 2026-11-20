// The Wheel of Yap itself, with nothing of OBS in it: the topics as slices of a wheel (a Face, painted once whenever
// the topics change), one spin worked out ahead (a Plan: the slice it lands on, how far into it, how many turns, and
// the reveal painted for it), and the Spinner, which moves the wheel, the pointer and the reveal frame by frame.
//
// Angles are in radians, clockwise on screen (y points down), from the pointer, which is on the right of the wheel at
// 3 o'clock. A slice's label runs along its middle from the hub outwards, so the slice under the pointer reads left to
// right. The wheel's picture is kSize x kSize; everything else is placed on that square.
#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <vector>

namespace sarxina::wheelofyap {

inline constexpr int kSize = 2048;              // the source's picture, square
inline constexpr double kRadius = 930;          // the slices
inline constexpr QPointF kCentre{960, 1024};   // of the wheel, a little left of the middle to leave the pointer room
inline constexpr int kLongestTopic = 40;        // characters of a topic on a slice
inline constexpr int kLongestForced = 80;       // characters of a forced topic in the reveal
inline constexpr double kForcedWeight = 3;      // a forced topic's slice is this many slices wide

struct Topic {
	QString label;
	bool on = true;
	bool operator==(const Topic &other) const { return label == other.label && on == other.on; }
};

// A topic as the wheel shows it: plain text, trimmed, cut to `longest` characters.
QString topicText(const QString &typed, int longest = kLongestTopic);
// The topics that are on, as slices.
QStringList slicesOf(const std::vector<Topic> &topics);

// What the wheel shows: its slices and their picture.
struct Face {
	QStringList labels;
	int forced = -1;            // the slice of a forced topic, or -1
	QString forcedText;         // the forced topic as the reveal shows it, longer than a slice's label
	std::vector<double> starts; // where each slice starts, and 2 pi at the end
	std::vector<QColor> colours;
	QImage disc;                // kSize x kSize, the wheel turned to 0

	int count() const { return static_cast<int>(labels.size()); }
	// The slice at an angle of the wheel itself.
	int sliceAt(double angle) const;
};

// Paints a face. With `forcedAt` >= 0, `forced` is put in as a slice of its own at that place.
std::shared_ptr<const Face> paintFace(const QStringList &labels, const QString &forced = {}, int forcedAt = -1);

// One spin, worked out before it starts.
struct Plan {
	uint64_t id = 0;
	int target = 0;          // the slice it lands on
	double into = 0.5;       // how far into that slice, 0 to 1
	int turns = 6;           // whole turns before it
	double seconds = 8;      // from the pull back to the stop
	QString who;             // the chatter who spun or forced it
	QString topic;           // the topic it lands on
	bool forced = false;
	QImage banner;           // the reveal
	QImage wedge;            // the slice it lands on, lit, as a picture of its own
	QPointF wedgeAt;         // where its top left is, from the wheel's centre, with the wheel at 0
};

// Works out a spin on `face`: a random slice, or the forced one.
Plan planSpin(const Face &face, const QString &who, uint32_t seed);

// The pictures that do not change.
QImage paintHub();
QImage paintPointer();
inline constexpr QPointF kPointerPivot{170, 80};               // in the pointer's picture
inline constexpr QPointF kPointerAt{kCentre.x() + kRadius + 103, kCentre.y()}; // where the pivot goes on the wheel's square
inline constexpr QPointF kBannerAt{kCentre.x(), kCentre.y() + 560};          // the reveal's centre

// The reveal: who and the topic, in big letters.
QImage paintBanner(const QString &who, const QString &topic, bool forced, const QColor &colour);

class Spinner {
public:
	// A spin of `face`, which the wheel shows from now on.
	void start(std::shared_ptr<const Face> face, const Plan &plan);
	// Shows `face` without spinning (new topics).
	void show(std::shared_ptr<const Face> face);
	// Time passes. True on the frame the wheel stops.
	bool advance(double seconds);

	bool spinning() const { return spinning_; }
	bool landedOnce() const { return landed_; }
	double angle() const { return angle_; }           // of the wheel
	double pointer() const { return pointer_; }       // of the pointer about its pivot
	double bannerScale() const;                       // of the reveal, 0 when it is not up
	double wedgeOpacity() const;                      // of the lit slice
	const Plan &plan() const { return plan_; }
	const std::shared_ptr<const Face> &face() const { return face_; }
	int sliceUnderPointer() const;
	int ticks() const { return ticks_; }              // slices that went past the pointer this spin

private:
	double wheelAt(double t) const;

	std::shared_ptr<const Face> face_;
	Plan plan_;
	bool spinning_ = false;
	bool landed_ = false;
	double t_ = 0;          // since the spin started
	double from_ = 0;       // the angle it started from
	double total_ = 0;      // how far it turns after the pull back
	double angle_ = 0;
	double pointer_ = 0, pointerSpeed_ = 0;
	int lastSlice_ = -1;
	int ticks_ = 0;
	double sinceLanded_ = 0;
};

// The whole picture as OBS draws it, for the self-test.
QImage compose(const Spinner &spinner, const QImage &hub, const QImage &pointer, double scale = 1.0);

} // namespace sarxina::wheelofyap
