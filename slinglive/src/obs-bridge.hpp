// The link between the panel and OBS: follows what OBS is really doing through frontend events, and turns presses
// into frontend API calls. Used only in real mode.
#pragma once

#include "sling-game.hpp"

#include <obs-frontend-api.h>

class ObsBridge : public sling::Host {
public:
	// Frontend events arrive here; `now` is the dock's clock in milliseconds.
	void handleEvent(enum obs_frontend_event event, double now);
	// Reads OBS's current state directly. Covers anything that changed without an event reaching us.
	void refresh(double now);
	// Called every frame while the dock is on screen: keeps the clock, and does a refresh once a second.
	void follow(double now);
	// Fills in what the panel should show.
	void apply(sling::Panel &panel) const;
	// Stops making calls into OBS (it is shutting down).
	void shutDown() { closed_ = true; }

	void setStreaming(bool on) override;
	void setRecording(bool on) override;
	void setVirtualCam(bool on) override;
	void setStudioMode(bool on) override;
	void openSettings() override;

private:
	enum class Stream {
		Offline,
		Requested, // asked for from the dock, OBS has not reported anything yet
		Starting,  // OBS is connecting
		Live,
	};

	Stream stream_ = Stream::Offline;
	double streamSince_ = 0;    // when the current stream state began
	bool stopWhenLive_ = false; // a stop was asked for while the stream was still starting
	bool recording_ = false;
	double recordingSince_ = 0;
	bool virtualCam_ = false;
	bool studioMode_ = false;
	double now_ = 0;
	double refreshedAt_ = 0;
	bool closed_ = false;
};
