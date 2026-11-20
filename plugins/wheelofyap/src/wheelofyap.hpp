// WheelOfYap's part on OBS's UI thread, as a plugin of the Sarxina OBS Plugin Manager: its settings (drawn behind the
// manager's gear, kept in its own config.json), the topics, and the two triggers (shared/src/chat-trigger.hpp): a
// spin, and a forced topic, whose viewer's text is put on the wheel and landed on. One spin at a time: the next waits
// until the wheel has stopped and its topic has been up for a while, and a forced topic goes before a spin.
#pragma once

#include "chat-trigger.hpp"
#include "wheel-source.hpp"
#include "wheel.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <cstdint>
#include <deque>
#include <random>
#include <vector>

namespace sarxina::wheelofyap {

class TopicsDialog;

class WheelOfYap : public QObject {
public:
	explicit WheelOfYap(std::vector<Topic> defaults);

	void start(); // from obs_module_post_load: hands the plugin to the manager, or runs it alone
	void shutDown();

	// What the manager calls.
	void setEnabled(bool enabled);
	obs_properties_t *properties();
	void getSettings(obs_data_t *settings) const;
	void update(obs_data_t *settings);
	void chat(const sarxina_chat_message &message);

	void landed(const Landed &landed);
	void editTopics();
	void setTopics(std::vector<Topic> topics);
	const std::vector<Topic> &topics() const { return topics_; }
	// A spin or a forced topic without Twitch (the test route): waits its turn like a redeem.
	void demo(const QString &who, const QString &forced);
	bool ready() const;

private:
	void pump();
	void go(const Heard &heard, bool forced);
	void showTopics();
	void load();
	void save() const;
	double now() const { return static_cast<double>(clock_.elapsed()); }

	ChatTrigger spin_{"spin", TriggerSettings::Trigger::Reward};
	ChatTrigger force_{"topic", TriggerSettings::Trigger::Reward};
	std::vector<Topic> topics_;
	std::deque<std::pair<Heard, bool>> demos_;
	bool managed_ = false;
	bool enabled_ = false;
	bool closed_ = false;
	bool busy_ = false;
	uint64_t spins_ = 0;
	Heard current_;
	bool currentForced_ = false;
	double startedAt_ = -1e300;
	double landedAt_ = -1e300;
	std::mt19937 random_{std::random_device{}()};
	QTimer pump_;
	QElapsedTimer clock_;
	QPointer<TopicsDialog> dialog_;
};

} // namespace sarxina::wheelofyap
