// JobApplication on OBS's UI thread, as a plugin of the Sarxina OBS Plugin Manager: a blank job application that comes
// up over the streamer's VTube Studio model and moves with it (paper-prop.hpp), and goes again, on a toggle:
//   - a hotkey, set in OBS's Settings > Hotkeys ("Show or hide the job application"), kept in the plugin's own
//     config.json;
//   - chat: a command or a channel point redeem, with a cooldown (shared/src/chat-trigger.hpp), set behind the
//     manager's gear along with the paper's size.
// The form (form.hpp) is painted once (paper.hpp) and handed to VTube Studio each time it comes up. Its fields are
// empty: filling them from chat comes later.
//
// Without the manager there is no chat and no VTube Studio, and the hotkey only says so in the log.
#pragma once

#include "chat-trigger.hpp"
#include "form.hpp"
#include "paper-prop.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace sarxina::jobapplication {

inline constexpr int kLeastSize = 10;   // percent of the largest item VTube Studio allows
inline constexpr int kMostSize = 100;
inline constexpr int kDefaultSize = 90;
inline constexpr uint32_t kPaperSeed = 1026; // the paper's grain and fibres, the same every time

// A request to VTube Studio over the manager's connection; false when the manager is not installed.
bool sendToVts(const QString &type, const QJsonObject &data, PaperProp::Answer answer);

class JobApplication : public QObject {
public:
	JobApplication();

	// Called once, from obs_module_post_load: the hotkey, then the plugin handed to the manager, or run alone.
	void start();
	void shutDown(); // OBS is closing

	// What the manager calls.
	void setEnabled(bool enabled);
	obs_properties_t *properties();
	void getSettings(obs_data_t *settings) const;
	void update(obs_data_t *settings);
	void chat(const sarxina_chat_message &message);

	// The hotkey or chat: the paper comes up, or goes.
	void toggle(const QString &who);
	// OBS saves: so does the plugin, with the hotkey's keys.
	void save() const;

	// For the check inside OBS (in-obs-check.cpp).
	bool managed() const { return managed_; }
	bool enabled() const { return enabled_; }
	PaperProp &prop() { return prop_; }
	const TriggerSettings &settings() const { return trigger_.settings(); }
	int sizePercent() const { return sizePercent_; }

private:
	void load();
	void pump(); // a redeem that waited
	bool ready() const { return !closed_ && enabled_ && managed_; }
	void paint(); // the paper's picture, once
	double now() const { return static_cast<double>(clock_.elapsed()); }

	ChatTrigger trigger_{"apply"};
	Form form_;
	PaperProp prop_;
	int sizePercent_ = kDefaultSize;
	obs_hotkey_id hotkey_ = OBS_INVALID_HOTKEY_ID;
	obs_data_array_t *savedKeys_ = nullptr; // read before the hotkey is registered
	bool managed_ = false;
	bool enabled_ = false;
	bool closed_ = false;
	QTimer pump_;
	QTimer check_;
	QElapsedTimer clock_;
};

} // namespace sarxina::jobapplication
