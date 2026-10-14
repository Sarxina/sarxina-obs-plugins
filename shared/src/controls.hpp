// The buttons the docks stand in for: OBS's Controls panel. What each one is called, what it says when pressed, and
// whether it switches something on and off. The games never look inside a button; they only press it.
#pragma once

#include <array>
#include <cstddef>

namespace sarxina {

// In the order of OBS's own Controls panel.
enum class Control { Stream, Record, VirtualCam, Studio, Settings, Exit };
inline constexpr size_t kControlCount = 6;

constexpr size_t index(Control control)
{
	return static_cast<size_t>(control);
}

struct ControlInfo {
	const char *label;    // locale key
	const char *labelOn;  // while it is on
	const char *toastOn;  // after a press that turned it on, or any press of a button that does not toggle
	const char *toastOff; // after a press that turned it off
	bool toggles;
};

// Exit has no toast: pressing it powers the panel off (ControlPanel::powerOff). OBS itself stays open.
inline constexpr ControlInfo kControls[kControlCount] = {
	{"StartStreaming", "StopStreaming", "Toast.StreamingStarted", "Toast.StreamingStopped", true},
	{"StartRecording", "StopRecording", "Toast.RecordingStarted", "Toast.RecordingStopped", true},
	{"StartVirtualCam", "StopVirtualCam", "Toast.VirtualCamOn", "Toast.VirtualCamOff", true},
	{"StudioMode", "StudioMode", "Toast.StudioModeOn", "Toast.StudioModeOff", true},
	{"Settings", "Settings", "Toast.Settings", "Toast.Settings", false},
	{"Exit", "Exit", nullptr, nullptr, false},
};

constexpr const ControlInfo &info(Control control)
{
	return kControls[index(control)];
}

// What the panel shows. In practice mode the panel changes it itself; in real mode it is copied from OBS.
struct PanelState {
	std::array<bool, kControlCount> on{};
	std::array<double, kControlCount> since{}; // clock time each one came on, in milliseconds
	bool connecting = false;                    // a stream was asked for and OBS is still connecting

	bool isOn(Control control) const { return on[index(control)]; }
	double onSince(Control control) const { return since[index(control)]; }
	void set(Control control, bool value, double now)
	{
		on[index(control)] = value;
		since[index(control)] = now;
	}
};

// Where presses go in real mode.
class Host {
public:
	virtual ~Host() = default;
	virtual void press(Control control, bool on) = 0;
};

} // namespace sarxina
