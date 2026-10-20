// JobApplication on OBS's UI thread, as a plugin of the Sarxina OBS Plugin Manager: a job application that comes up
// over the streamer's VTube Studio model and moves with it (paper-prop.hpp), and that chat fills in.
//   - The hotkey, set in OBS's Settings > Hotkeys ("Show or hide the job application") and kept in the plugin's own
//     config.json, brings it up and takes it away.
//   - Chat writes on it: each channel point redeem (the trigger it starts with, since a reward's cost and cooldown keep
//     chat from flooding it) or, if the streamer picks it, each "!jobapp <text>", adds a line to Experience with the
//     viewer's name (shared/src/chat-trigger.hpp). The command's cooldown is for each chatter. A line brings the paper
//     up if it is down. The lines are kept until OBS closes; the full name on the form is "Chat".
//   - The trigger, the reward (picked from the channel's, or made with a button) and the paper's size are set behind
//     the manager's gear.
// New lines are gathered for a moment and written together, at most every couple of seconds: the paper is painted
// again (form.hpp, paper.hpp) and VTube Studio gets it in place of the one up. A redeem of a reward the manager made is
// marked fulfilled once its line is on the paper on screen.
//
// For a demo or a test, obs-websocket can hand it chat messages (test-route.cpp).
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

#include <deque>
#include <utility>
#include <vector>

namespace sarxina::jobapplication {

inline constexpr int kLeastSize = 10;   // percent of the largest item VTube Studio allows
inline constexpr int kMostSize = 100;
inline constexpr int kDefaultSize = 90;
inline constexpr uint32_t kPaperSeed = 1026; // the paper's grain and fibres, the same every time
inline constexpr int kLongestEntry = 60;     // characters of a line of experience
inline constexpr int kLongestSigned = 25;    // characters of the name after it
inline constexpr size_t kEntriesKept = 100;  // far more than fit on the paper
inline constexpr int kGatherMs = 400;        // lines that come together are written together
inline constexpr int kWriteEveryMs = 2500;   // and the paper is written again at most this often

// A request to VTube Studio over the manager's connection; false when the manager is not installed.
bool sendToVts(const QString &type, const QJsonObject &data, PaperProp::Answer answer);

class JobApplication : public QObject {
public:
	// `send` reaches VTube Studio (sendToVts unless the self-test says otherwise).
	explicit JobApplication(PaperProp::Send send = sendToVts);

	// Called once, from obs_module_post_load: the hotkey, then the plugin handed to the manager, or run alone.
	void start();
	void shutDown(); // OBS is closing

	// What the manager calls.
	void setEnabled(bool enabled);
	obs_properties_t *properties();
	void getSettings(obs_data_t *settings) const;
	void update(obs_data_t *settings);
	void chat(const sarxina_chat_message &message);

	// The hotkey: the paper comes up, or goes.
	void toggle(const QString &who);
	// OBS saves: so does the plugin, with the hotkey's keys.
	void save() const;

	// For the checks (self-test.cpp, in-obs-check.cpp).
	bool managed() const { return managed_; }
	bool enabled() const { return enabled_; }
	PaperProp &prop() { return prop_; }
	ChatTrigger &trigger() { return trigger_; }
	const TriggerSettings &settings() const { return trigger_.settings(); }
	int sizePercent() const { return sizePercent_; }
	const Form &form() const { return form_; }
	int painted() const { return painted_; }
	size_t entries() const { return entries_.size(); }
	bool writingSoon() const { return writeLater_.isActive(); }
	// The self-test runs it without OBS: no config, no hotkey, quicker writing.
	void runForTest(int gatherMs, int writeEveryMs);

private:
	void load();
	void pump(); // a redeem that waited
	bool ready() const { return !closed_ && enabled_ && managed_; }
	void took(const Heard &heard); // a line for the paper
	void writeSoon();
	void write();  // the lines so far, on the paper on screen
	void paint();  // the paper's picture and texture, if there is new writing
	void wrote(int writing);
	void settingsChanged();
	double now() const { return static_cast<double>(clock_.elapsed()); }

	ChatTrigger trigger_{"jobapp", TriggerSettings::Trigger::Reward, true};
	Form form_;
	PaperProp prop_;
	std::deque<Bullet> entries_; // the newest first
	std::vector<std::pair<int, Heard>> toFulfil_; // redeems, by the writing that has their line
	int painted_ = 0;   // how many times the paper was painted: the writing VTube Studio gets
	bool dirty_ = true; // new lines since then
	double lastWrite_ = -1e300;
	int gatherMs_ = kGatherMs;
	int writeEveryMs_ = kWriteEveryMs;
	bool testing_ = false;
	int sizePercent_ = kDefaultSize;
	obs_hotkey_id hotkey_ = OBS_INVALID_HOTKEY_ID;
	obs_data_array_t *savedKeys_ = nullptr; // read before the hotkey is registered
	bool managed_ = false;
	bool enabled_ = false;
	bool closed_ = false;
	QTimer pump_;
	QTimer check_;
	QTimer writeLater_;
	QElapsedTimer clock_;
};

// The test route (test-route.cpp): obs-websocket's vendor request "chat" of the vendor "JobApplication", added from
// obs_module_post_load.
void addTestRoute(JobApplication &plugin);

} // namespace sarxina::jobapplication
