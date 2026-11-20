// The wheel as an OBS source: Sarxina Source > Wheel of Yap (or Sources > + > Wheel of Yap). Every wheel shows the same
// topics, which the plugin hands over from the UI thread; the faces and reveals are painted there too, so the graphics
// thread only turns pictures it already has. A spin goes only to the wheels in the scene being streamed or recorded
// (sources OBS calls active).
//
// Everything here is called on the UI thread, and the landing handler runs there too.
#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>

namespace sarxina::wheelofyap {

inline constexpr const char *kSourceId = "wheelofyap";

void registerWheelSource();
void releaseWheelGraphics(); // OBS is unloading the plugin

// The slices every wheel shows from now on.
void showTopics(const QStringList &slices);
int wheelCount();
bool wheelOnScreen();

// Spins every wheel on screen, onto a random topic, or with `forced`, onto that topic put on the wheel for this spin.
// Says on how many.
int spinWheels(uint64_t id, const QString &who, const QString &forced, uint32_t seed);

struct Landed {
	uint64_t id = 0;
	QString who, topic;
	bool forced = false;
};
void setLandingHandler(void (*handler)(const Landed &landed));
// The source's own Properties have the button that opens the topics.
void setEditTopicsHandler(void (*handler)());
void stopLandings(); // OBS is closing

} // namespace sarxina::wheelofyap
