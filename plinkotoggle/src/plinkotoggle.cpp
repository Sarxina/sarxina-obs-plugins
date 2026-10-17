#include "plinkotoggle.hpp"

#include "board-source.hpp"
#include "plain-text.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstring>
#include <memory>

namespace sarxina::plinkotoggle {

namespace {

constexpr int kAskMs = 2000; // how often VTube Studio is asked for the model's hotkeys
constexpr int kPumpMs = 250; // how often a redeem that waits is looked at

QByteArray utf8(const QString &text)
{
	return text.toUtf8();
}

PlinkoToggle *self(void *data)
{
	return static_cast<PlinkoToggle *>(data);
}

// The settings show the command field or the reward fields, whichever the trigger needs.
bool triggerPicked(obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	const bool reward = std::strcmp(obs_data_get_string(settings, "trigger"), "reward") == 0;
	obs_property_set_visible(obs_properties_get(properties, "command"), !reward);
	obs_property_set_visible(obs_properties_get(properties, "reward"), reward);
	obs_property_set_visible(obs_properties_get(properties, "reward_help"), reward);
	obs_property_set_visible(obs_properties_get(properties, "reward_name"), reward);
	return true;
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
	nullptr, // no fixed commands: the command is a setting, so it listens to all of chat
	0,
	[](void *data, const sarxina_chat_message *message) { self(data)->chat(*message); },
};

void answeredHotkeys(void *param, bool ok, const char *json)
{
	self(param)->answered(ok, json);
}

} // namespace

PlinkoToggle::PlinkoToggle()
{
	clock_.start();
	load();
	poll_.setInterval(kAskMs);
	connect(&poll_, &QTimer::timeout, this, &PlinkoToggle::ask);
	pump_.setInterval(kPumpMs);
	connect(&pump_, &QTimer::timeout, this, &PlinkoToggle::pump);
}

void PlinkoToggle::start()
{
	info.description = obs_module_text("Description");
	info.data = this;
	if (sarxina_obs_plugin_manager_register(&info)) {
		blog(LOG_INFO, "[%s] %s loaded, run by the Sarxina OBS Plugin Manager", PLUGIN_TITLE, PLUGIN_VERSION);
	} else {
		// No chat and no VTube Studio without it: the boards stay empty, and the log says why.
		blog(LOG_WARNING, "[%s] %s loaded, but the Sarxina OBS Plugin Manager is not installed", PLUGIN_TITLE,
		     PLUGIN_VERSION);
		managed_ = false;
		enabled_ = true;
		refresh();
	}
}

void PlinkoToggle::shutDown()
{
	closed_ = true;
	poll_.stop();
	pump_.stop();
	stopLandings();
}

// ---- on and off -----------------------------------------------------------------------------------------------------

void PlinkoToggle::setEnabled(bool enabled)
{
	managed_ = true; // only the manager calls this
	enabled_ = enabled;
	model_ = Model();
	if (enabled) {
		poll_.start();
		pump_.start();
		ask();
	} else {
		poll_.stop();
		pump_.stop();
	}
	blog(LOG_INFO, "[%s] %s", PLUGIN_TITLE, enabled ? "on" : "off");
	refresh();
}

// ---- settings -------------------------------------------------------------------------------------------------------

obs_properties_t *PlinkoToggle::properties()
{
	obs_properties_t *properties = obs_properties_create();
	obs_property_t *trigger = obs_properties_add_list(properties, "trigger", obs_module_text("Trigger"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(trigger, obs_module_text("Trigger.Command"), "command");
	obs_property_list_add_string(trigger, obs_module_text("Trigger.Reward"), "reward");
	obs_property_set_modified_callback(trigger, triggerPicked);

	obs_properties_add_text(properties, "command", obs_module_text("Command"), OBS_TEXT_DEFAULT);

	// The rewards redeemed in chat since OBS started, and the one picked before, which may not have been.
	obs_property_t *reward = obs_properties_add_list(properties, "reward", obs_module_text("Reward"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(reward, obs_module_text("Reward.None"), "");
	const Settings &now = drops_.settings();
	const std::vector<Drops::Reward> &seen = drops_.rewards();
	const bool listed = now.rewardId.empty() || std::any_of(seen.begin(), seen.end(), [&now](const auto &r) {
				    return r.id == now.rewardId;
			    });
	if (!listed)
		obs_property_list_add_string(reward, utf8(now.rewardLabel).constData(), now.rewardId.c_str());
	for (const Drops::Reward &r : seen)
		obs_property_list_add_string(reward, utf8(r.label).constData(), r.id.c_str());
	obs_properties_add_text(properties, "reward_help", obs_module_text("Reward.Help"), OBS_TEXT_INFO);
	obs_properties_add_text(properties, "reward_name", obs_module_text("RewardName"), OBS_TEXT_DEFAULT);

	obs_property_t *cooldown =
		obs_properties_add_int(properties, "cooldown", obs_module_text("Cooldown"), kLeastCooldown, kMostCooldown, 1);
	obs_property_int_set_suffix(cooldown, " s");
	obs_property_set_long_description(cooldown, obs_module_text("Cooldown.Help"));
	return properties;
}

void PlinkoToggle::getSettings(obs_data_t *settings) const
{
	const Settings &now = drops_.settings();
	obs_data_set_string(settings, "trigger", now.trigger == Settings::Trigger::Reward ? "reward" : "command");
	obs_data_set_string(settings, "command", ("!" + now.command).c_str());
	obs_data_set_string(settings, "reward", now.rewardId.c_str());
	obs_data_set_string(settings, "reward_name", utf8(now.rewardName).constData());
	obs_data_set_int(settings, "cooldown", now.cooldownSeconds);
}

void PlinkoToggle::update(obs_data_t *settings)
{
	Settings changed = drops_.settings();
	changed.trigger = std::strcmp(obs_data_get_string(settings, "trigger"), "reward") == 0 ? Settings::Trigger::Reward
											      : Settings::Trigger::Command;

	// A command that cannot be one keeps the one before, and the field shows what the command is now.
	const std::string typed = obs_data_get_string(settings, "command");
	const std::string command = commandName(typed);
	if (!command.empty())
		changed.command = command;
	const bool rewrite = typed != "!" + changed.command;

	const std::string reward = obs_data_get_string(settings, "reward");
	if (reward != changed.rewardId) {
		changed.rewardId = reward;
		changed.rewardLabel.clear();
		for (const Drops::Reward &r : drops_.rewards())
			if (r.id == reward)
				changed.rewardLabel = r.label;
	}
	changed.rewardName = plainText(QString::fromUtf8(obs_data_get_string(settings, "reward_name")), kLongestRewardName);
	changed.cooldownSeconds =
		std::clamp(static_cast<int>(obs_data_get_int(settings, "cooldown")), kLeastCooldown, kMostCooldown);

	apply(changed);
	save();
	if (rewrite) // once the settings are done with this change
		QTimer::singleShot(0, this, [] { sarxina_obs_plugin_manager_settings_changed(PLUGIN_NAME); });
}

void PlinkoToggle::trySettings(const Settings &settings)
{
	apply(settings);
}

void PlinkoToggle::apply(const Settings &settings)
{
	drops_.setSettings(settings);
	refresh();
}

void PlinkoToggle::load()
{
	Settings settings;
	BPtr<char> path(obs_module_config_path("config.json"));
	if (path && os_file_exists(path)) {
		OBSDataAutoRelease config = obs_data_create_from_json_file_safe(path, "bak");
		if (config) {
			obs_data_set_default_string(config, "trigger", "command");
			obs_data_set_default_string(config, "command", settings.command.c_str());
			obs_data_set_default_int(config, "cooldown", settings.cooldownSeconds);
			if (std::strcmp(obs_data_get_string(config, "trigger"), "reward") == 0)
				settings.trigger = Settings::Trigger::Reward;
			if (const std::string command = commandName(obs_data_get_string(config, "command")); !command.empty())
				settings.command = command;
			settings.rewardId = obs_data_get_string(config, "reward_id");
			settings.rewardLabel = QString::fromUtf8(obs_data_get_string(config, "reward_label"));
			settings.rewardName = plainText(QString::fromUtf8(obs_data_get_string(config, "reward_name")),
							kLongestRewardName);
			settings.cooldownSeconds = std::clamp(static_cast<int>(obs_data_get_int(config, "cooldown")),
							      kLeastCooldown, kMostCooldown);
		}
	}
	drops_.setSettings(settings);
}

void PlinkoToggle::save() const
{
	BPtr<char> folder(obs_module_config_path(""));
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!folder || !path)
		return;
	os_mkdirs(folder);
	const Settings &now = drops_.settings();
	OBSDataAutoRelease config = obs_data_create();
	obs_data_set_string(config, "trigger", now.trigger == Settings::Trigger::Reward ? "reward" : "command");
	obs_data_set_string(config, "command", now.command.c_str());
	obs_data_set_string(config, "reward_id", now.rewardId.c_str());
	obs_data_set_string(config, "reward_label", utf8(now.rewardLabel).constData());
	obs_data_set_string(config, "reward_name", utf8(now.rewardName).constData());
	obs_data_set_int(config, "cooldown", now.cooldownSeconds);
	obs_data_save_json_safe(config, path, "tmp", "bak");
}

// ---- VTube Studio ---------------------------------------------------------------------------------------------------

// Only while there is a board to show the answer on.
void PlinkoToggle::ask()
{
	if (closed_ || !enabled_ || !managed_ || asking_ || boardCount() == 0)
		return;
	const sarxina_vts_request request{"HotkeysInCurrentModelRequest", nullptr, answeredHotkeys, this};
	asking_ = sarxina_obs_plugin_manager_vts_request(&request);
}

void PlinkoToggle::answered(bool ok, const char *json)
{
	asking_ = false;
	if (closed_ || !enabled_)
		return;
	// Every new reason VTube Studio gives no hotkeys goes in the log once.
	const std::string problem = ok ? std::string() : std::string(json ? json : "");
	if (!problem.empty() && problem != problem_)
		blog(LOG_INFO, "[%s] VTube Studio: no hotkeys (%s)", PLUGIN_TITLE, problem.c_str());
	problem_ = problem;
	Model model = readHotkeys(ok, json);
	if (model == model_)
		return;
	if (model.state == Model::State::Loaded)
		blog(LOG_INFO, "[%s] VTube Studio: %s, %zu hotkeys", PLUGIN_TITLE, utf8(model.name).constData(),
		     model.toggles.size());
	else if (model.state == Model::State::NoModel)
		blog(LOG_INFO, "[%s] VTube Studio: no model loaded", PLUGIN_TITLE);
	model_ = std::move(model);
	refresh();
}

void PlinkoToggle::refresh()
{
	// The board shows nothing but the toggles and how chat drops a ball. Without the manager, VTube Studio or a model
	// it is empty; the log says why.
	Content content;
	const Settings &now = drops_.settings();
	if (managed_ && enabled_) {
		if (model_.state == Model::State::Loaded)
			content.toggles = model_.toggles;
		content.label = now.trigger == Settings::Trigger::Command ? QString::fromStdString("!" + now.command)
									   : now.rewardName;
	}
	showOnBoards(content);
}

// ---- chat and balls -------------------------------------------------------------------------------------------------

bool PlinkoToggle::ready() const
{
	return !closed_ && enabled_ && model_.state == Model::State::Loaded && !model_.toggles.empty() &&
	       boardOnScreen();
}

void PlinkoToggle::chat(const sarxina_chat_message &message)
{
	if (closed_ || !enabled_)
		return;
	QString who;
	if (drops_.hear(message, now(), ready(), who))
		dropFor(who);
	if (drops_.rewardsChanged()) // a new one to pick from, if the settings are open
		QTimer::singleShot(0, this, [] { sarxina_obs_plugin_manager_settings_changed(PLUGIN_NAME); });
}

void PlinkoToggle::pump()
{
	QString who;
	if (drops_.waiting() && drops_.next(now(), ready(), who))
		dropFor(who);
}

void PlinkoToggle::dropFor(const QString &who)
{
	std::optional<uint32_t> seed;
	if (!seeds.empty()) {
		seed = seeds.front();
		seeds.pop_front();
	}
	const int boards = dropOnBoards(who, seed);
	dropped_++;
	blog(LOG_INFO, "[%s] a ball for %s, on %d board(s)", PLUGIN_TITLE, utf8(who).constData(), boards);
}

void PlinkoToggle::landed(const Landing &landing)
{
	if (closed_ || !enabled_ || !managed_)
		return;
	blog(LOG_INFO, "[%s] %s's ball came down on %s", PLUGIN_TITLE, utf8(landing.who).constData(),
	     utf8(landing.toggle.name).constData());

	struct Sent {
		PlinkoToggle *plugin;
		Landing landing;
	};
	const QByteArray data =
		QJsonDocument(QJsonObject{{QStringLiteral("hotkeyID"), QString::fromStdString(landing.toggle.id)}})
			.toJson(QJsonDocument::Compact);
	const sarxina_vts_request request{
		"HotkeyTriggerRequest", data.constData(),
		[](void *param, bool ok, const char *json) {
			std::unique_ptr<Sent> sent(static_cast<Sent *>(param));
			if (!ok)
				blog(LOG_WARNING, "[%s] VTube Studio did not switch %s: %s", PLUGIN_TITLE,
				     utf8(sent->landing.toggle.name).constData(), json ? json : "");
			if (sent->plugin->fired)
				sent->plugin->fired(sent->landing, ok);
		},
		new Sent{this, landing}};
	if (!sarxina_obs_plugin_manager_vts_request(&request))
		delete static_cast<Sent *>(request.param);
}

} // namespace sarxina::plinkotoggle
