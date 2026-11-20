#include "wheelofyap.hpp"

#include "plain-text.hpp"
#include "topics-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>

namespace sarxina::wheelofyap {

namespace {

constexpr int kPumpMs = 250;       // how often a spin that waits is looked at
constexpr double kHoldMs = 7000;   // a landed topic stays up at least this long before the next spin
constexpr double kStuckMs = 30000; // a spin no wheel said it finished is given up on after this long
constexpr int kSpinCost = 300, kForceCost = 3000;

QByteArray utf8(const QString &text)
{
	return text.toUtf8();
}

WheelOfYap *self(void *data)
{
	return static_cast<WheelOfYap *>(data);
}

sarxina_plugin_info info = {
	SARXINA_OBS_PLUGIN_MANAGER_API,
	PLUGIN_NAME,
	PLUGIN_TITLE,
	nullptr, // the description, from the strings
	nullptr, // the plugin, set in start()
	[](void *data, bool enabled) { self(data)->setEnabled(enabled); },
	[](void *data) { return self(data)->properties(); },
	[](void *data, obs_data_t *settings) { self(data)->getSettings(settings); },
	[](void *data, obs_data_t *settings) { self(data)->update(settings); },
	nullptr, // no fixed commands: the commands are settings, so it listens to all of chat
	0,
	[](void *data, const sarxina_chat_message *message) { self(data)->chat(*message); },
};

bool editPressed(obs_properties_t *, obs_property_t *, void *data)
{
	self(data)->editTopics();
	return false;
}

void heading(obs_properties_t *properties, const char *key, const char *words)
{
	obs_properties_add_text(properties, key, obs_module_text(words), OBS_TEXT_INFO);
}

void settingsChangedLater(QObject *context)
{
	QTimer::singleShot(0, context, [] { sarxina_obs_plugin_manager_settings_changed(PLUGIN_NAME); });
}

} // namespace

WheelOfYap::WheelOfYap(std::vector<Topic> defaults) : topics_(std::move(defaults))
{
	clock_.start();
	spin_.key = "spin_";
	spin_.strings = "Spin.";
	spin_.costInSettings = true;
	spin_.newRewardCost = kSpinCost;
	force_.key = "force_";
	force_.strings = "Force.";
	force_.costInSettings = true;
	force_.newRewardCost = kForceCost;
	load();
	pump_.setInterval(kPumpMs);
	connect(&pump_, &QTimer::timeout, this, &WheelOfYap::pump);
	for (ChatTrigger *trigger : {&spin_, &force_})
		trigger->changed = [this] {
			save();
			settingsChangedLater(this);
		};
}

void WheelOfYap::start()
{
	info.description = obs_module_text("Description");
	info.data = this;
	showTopics();
	if (sarxina_obs_plugin_manager_register(&info)) {
		blog(LOG_INFO, "[%s] %s loaded, run by the Sarxina OBS Plugin Manager", PLUGIN_TITLE, PLUGIN_VERSION);
	} else {
		// No chat without it; the test route still spins the wheel.
		blog(LOG_WARNING, "[%s] %s loaded, but the Sarxina OBS Plugin Manager is not installed", PLUGIN_TITLE,
		     PLUGIN_VERSION);
		enabled_ = true;
		pump_.start();
	}
}

void WheelOfYap::shutDown()
{
	closed_ = true;
	pump_.stop();
	stopLandings();
	if (dialog_)
		dialog_->close();
}

void WheelOfYap::setEnabled(bool enabled)
{
	managed_ = true;
	enabled_ = enabled;
	if (enabled)
		pump_.start();
	else
		pump_.stop();
	blog(LOG_INFO, "[%s] %s", PLUGIN_TITLE, enabled ? "on" : "off");
}

// ---- settings -------------------------------------------------------------------------------------------------------

obs_properties_t *WheelOfYap::properties()
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_button2(properties, "topics_edit", obs_module_text("Topics.Edit"), editPressed, this);
	const int on = static_cast<int>(slicesOf(topics_).size());
	obs_properties_add_text(properties, "topics_count",
				utf8(QString::fromUtf8(obs_module_text("Topics.Count")).arg(topics_.size()).arg(on)).constData(),
				OBS_TEXT_INFO);
	heading(properties, "spin_heading", "Spin.Heading");
	addTriggerProperties(properties, spin_, false);
	heading(properties, "force_heading", "Force.Heading");
	addTriggerProperties(properties, force_, false);
	return properties;
}

void WheelOfYap::getSettings(obs_data_t *settings) const
{
	triggerToSettings(settings, spin_.settings(), spin_.key);
	triggerToSettings(settings, force_.settings(), force_.key);
	obs_data_set_int(settings, "spin_reward_cost", spin_.newRewardCost);
	obs_data_set_int(settings, "force_reward_cost", force_.newRewardCost);
}

void WheelOfYap::update(obs_data_t *settings)
{
	bool rewriteSpin = false, rewriteForce = false;
	spin_.setSettings(triggerFromSettings(settings, spin_, rewriteSpin));
	force_.setSettings(triggerFromSettings(settings, force_, rewriteForce));
	spin_.newRewardCost = std::max(1, static_cast<int>(obs_data_get_int(settings, "spin_reward_cost")));
	force_.newRewardCost = std::max(1, static_cast<int>(obs_data_get_int(settings, "force_reward_cost")));
	save();
	if (rewriteSpin || rewriteForce)
		settingsChangedLater(this);
}

void WheelOfYap::load()
{
	OBSDataAutoRelease config;
	BPtr<char> path(obs_module_config_path("config.json"));
	if (path && os_file_exists(path))
		config = obs_data_create_from_json_file_safe(path, "bak");
	spin_.setSettings(loadTrigger(config, spin_.settings(), spin_.key));
	force_.setSettings(loadTrigger(config, force_.settings(), force_.key));
	if (!config)
		return;
	obs_data_set_default_int(config, "spin_reward_cost", kSpinCost);
	obs_data_set_default_int(config, "force_reward_cost", kForceCost);
	spin_.newRewardCost = std::max(1, static_cast<int>(obs_data_get_int(config, "spin_reward_cost")));
	force_.newRewardCost = std::max(1, static_cast<int>(obs_data_get_int(config, "force_reward_cost")));
	if (obs_data_has_user_value(config, "topics")) {
		OBSDataArrayAutoRelease list = obs_data_get_array(config, "topics");
		std::vector<Topic> topics;
		for (size_t i = 0, count = obs_data_array_count(list); i < count; i++) {
			OBSDataAutoRelease item = obs_data_array_item(list, i);
			const QString label = topicText(QString::fromUtf8(obs_data_get_string(item, "label")));
			if (!label.isEmpty())
				topics.push_back({label, obs_data_get_bool(item, "on")});
		}
		topics_ = std::move(topics);
	}
}

void WheelOfYap::save() const
{
	BPtr<char> folder(obs_module_config_path(""));
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!folder || !path)
		return;
	os_mkdirs(folder);
	OBSDataAutoRelease config = obs_data_create();
	saveTrigger(config, spin_.settings(), spin_.key);
	saveTrigger(config, force_.settings(), force_.key);
	obs_data_set_int(config, "spin_reward_cost", spin_.newRewardCost);
	obs_data_set_int(config, "force_reward_cost", force_.newRewardCost);
	OBSDataArrayAutoRelease list = obs_data_array_create();
	for (const Topic &topic : topics_) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "label", utf8(topic.label).constData());
		obs_data_set_bool(item, "on", topic.on);
		obs_data_array_push_back(list, item);
	}
	obs_data_set_array(config, "topics", list);
	obs_data_save_json_safe(config, path, "tmp", "bak");
}

// ---- topics ---------------------------------------------------------------------------------------------------------

void WheelOfYap::editTopics()
{
	if (dialog_) {
		dialog_->raise();
		dialog_->activateWindow();
		return;
	}
	auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	dialog_ = new TopicsDialog(
		topics_, [this](std::vector<Topic> topics) { setTopics(std::move(topics)); }, parent);
	dialog_->show();
}

void WheelOfYap::setTopics(std::vector<Topic> topics)
{
	topics_ = std::move(topics);
	save();
	showTopics();
	blog(LOG_INFO, "[%s] %zu topics, %d on the wheel", PLUGIN_TITLE, topics_.size(),
	     static_cast<int>(slicesOf(topics_).size()));
	settingsChangedLater(this);
}

void WheelOfYap::showTopics()
{
	wheelofyap::showTopics(slicesOf(topics_));
}

// ---- spins ----------------------------------------------------------------------------------------------------------

bool WheelOfYap::ready() const
{
	const double at = now();
	return !closed_ && enabled_ && wheelOnScreen() && (!busy_ || at - startedAt_ > kStuckMs) &&
	       at - landedAt_ >= kHoldMs;
}

void WheelOfYap::chat(const sarxina_chat_message &message)
{
	if (closed_ || !enabled_)
		return;
	Heard heard;
	if (force_.hear(message, now(), ready(), heard))
		go(heard, true);
	if (spin_.hear(message, now(), ready(), heard))
		go(heard, false);
	if ((spin_.rewardsChanged() && !spin_.listed()) || (force_.rewardsChanged() && !force_.listed()))
		settingsChangedLater(this);
}

void WheelOfYap::demo(const QString &who, const QString &forced)
{
	Heard heard;
	heard.who = plainText(who.isEmpty() ? QStringLiteral("Test") : who, 25);
	heard.text = forced;
	demos_.push_back({heard, !forced.trimmed().isEmpty()});
	pump();
}

void WheelOfYap::pump()
{
	if (!ready())
		return;
	Heard heard;
	if (!demos_.empty()) {
		const auto [demo, forced] = demos_.front();
		demos_.pop_front();
		go(demo, forced);
	} else if (force_.waiting() && force_.next(now(), true, heard)) {
		go(heard, true);
	} else if (spin_.waiting() && spin_.next(now(), true, heard)) {
		go(heard, false);
	}
}

void WheelOfYap::go(const Heard &heard, bool forced)
{
	const QString topic = forced ? topicText(heard.text, kLongestForced) : QString();
	forced = !topic.isEmpty(); // a forced topic with nothing written is a spin
	busy_ = true;
	startedAt_ = now();
	current_ = heard;
	currentForced_ = forced;
	const uint64_t id = ++spins_;
	const int wheels = spinWheels(id, heard.who, topic, static_cast<uint32_t>(random_()));
	blog(LOG_INFO, "[%s] %s %s%s, on %d wheel(s)", PLUGIN_TITLE, utf8(heard.who).constData(),
	     forced ? "forced the topic: " : "spun the wheel", forced ? utf8(topic).constData() : "", wheels);
	if (wheels == 0)
		busy_ = false;
}

void WheelOfYap::landed(const Landed &landed)
{
	if (closed_ || !busy_ || landed.id != spins_)
		return; // another wheel of the same spin, or one that was given up on
	busy_ = false;
	landedAt_ = now();
	blog(LOG_INFO, "[%s] %s's spin landed on: %s", PLUGIN_TITLE, utf8(landed.who).constData(),
	     utf8(landed.topic).constData());
	(currentForced_ ? force_ : spin_).fulfil(current_);
}

} // namespace sarxina::wheelofyap
