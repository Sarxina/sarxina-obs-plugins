// PlinkoToggle's part on OBS's UI thread, as a plugin of the Sarxina OBS Plugin Manager: its settings (drawn behind
// the manager's gear, kept in its own config.json), the chat that drops balls (drops.hpp), and VTube Studio, reached
// over the manager's one connection. Every two seconds it asks VTube Studio for the hotkeys of the model it has loaded,
// so the boards follow a new model or a changed hotkey; a ball that comes down in a slot sends HotkeyTriggerRequest
// for that slot's hotkey.
//
// Without the manager there is no chat and no VTube Studio, and the boards stay empty.
#pragma once

#include "drops.hpp"
#include "toggle-board.hpp"
#include "vts-toggles.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace sarxina::plinkotoggle {

class PlinkoToggle : public QObject {
public:
	PlinkoToggle();

	// Called once, from obs_module_post_load: hands the plugin to the manager, or runs it alone.
	void start();
	void shutDown(); // OBS is closing

	// What the manager calls.
	void setEnabled(bool enabled);
	obs_properties_t *properties();
	void getSettings(obs_data_t *settings) const;
	void update(obs_data_t *settings);
	void chat(const sarxina_chat_message &message);

	// VTube Studio's answer to the question for the model's hotkeys.
	void answered(bool ok, const char *json);
	// A ball came down in a slot.
	void landed(const Landing &landing);
	// Whether chat can drop a ball now: there are toggles and a board on screen.
	bool ready() const;

	// For the check inside OBS (in-obs-check.cpp).
	bool managed() const { return managed_; }
	bool enabled() const { return enabled_; }
	const Model &model() const { return model_; }
	const Settings &settings() const { return drops_.settings(); }
	// Settings for now only: not saved.
	void trySettings(const Settings &settings);
	// Set: hears every hotkey a landing sent, with VTube Studio's answer.
	std::function<void(const Landing &landing, bool ok)> fired;
	// The seeds of the next balls, so that they fall a known way; and how many balls have dropped.
	std::deque<uint32_t> seeds;
	int dropped() const { return dropped_; }
	size_t waiting() const { return drops_.waiting(); }

private:
	void ask();
	void refresh(); // what the boards show, from the model and the settings
	void pump();    // a redeem that waited
	void dropFor(const QString &who);
	void apply(const Settings &settings);
	void load();
	void save() const;
	double now() const { return static_cast<double>(clock_.elapsed()); }

	Drops drops_;
	Model model_;
	std::string problem_; // the last reason VTube Studio gave no hotkeys
	bool managed_ = false; // the manager runs it
	bool enabled_ = false;
	bool asking_ = false;
	bool closed_ = false;
	int dropped_ = 0;
	QTimer poll_;
	QTimer pump_;
	QElapsedTimer clock_;
};

} // namespace sarxina::plinkotoggle
