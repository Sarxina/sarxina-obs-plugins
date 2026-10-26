// What PlinkoToggle's slots are: the hotkeys VTube Studio has for the model it has loaded, in VTube Studio's order,
// read from its answer to HotkeysInCurrentModelRequest. A hotkey set to do nothing ("Unset") is left out.
#pragma once

#include <QString>

#include <string>
#include <vector>

namespace sarxina::plinkotoggle {

struct Toggle {
	std::string id;   // VTube Studio's hotkeyID, which HotkeyTriggerRequest takes
	QString name;     // as its slot shows it
	std::string type; // what the hotkey does: "ToggleExpression", "TriggerAnimation", ...
	std::string file; // the expression or animation it works on, if any

	bool operator==(const Toggle &other) const
	{
		return id == other.id && name == other.name && type == other.type && file == other.file;
	}
};

struct Model {
	enum class State {
		Unknown,      // not asked yet
		NotConnected, // the manager has no connection to VTube Studio, or VTube Studio answered with an error
		NoModel,
		Loaded,
	};
	State state = State::Unknown;
	std::string id;
	QString name;
	std::vector<Toggle> toggles;

	bool operator==(const Model &other) const
	{
		return state == other.state && id == other.id && name == other.name && toggles == other.toggles;
	}
};

// VTube Studio's answer as the manager hands it over: whether it is one, and its data as JSON.
Model readHotkeys(bool ok, const char *json);

} // namespace sarxina::plinkotoggle
