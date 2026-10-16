#include "manager.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>

#include <QJsonDocument>
#include <QTimer>

#include <cstring>

namespace sarxina {

namespace {

constexpr const char *kLog = "[Sarxina OBS Plugin Manager]";

} // namespace

Manager::Manager(Use use, const QString &twitchServer) : use_(use), twitch_(twitchServer)
{
	clock_.start();
	load();
	connect(&twitch_, &TwitchChat::message, this, &Manager::route);
	connect(&vts_, &VTubeStudio::tokenChanged, this, [this](const QString &token) {
		vtsToken_ = token;
		save();
	});
}

// ---- plugins --------------------------------------------------------------------------------------------------------

bool Manager::add(const sarxina_plugin_info *info)
{
	if (closed_ || !info)
		return false;
	if (info->api != SARXINA_OBS_PLUGIN_MANAGER_API || !info->id || !info->name || !info->set_enabled) {
		blog(LOG_WARNING, "%s cannot take %s: it is made for another version of the manager", kLog,
		     info->id ? info->id : "a plugin");
		return false;
	}
	for (const auto &plugin : plugins_) {
		if (std::strcmp(plugin->info->id, info->id) == 0) {
			blog(LOG_WARNING, "%s %s is installed twice", kLog, info->name);
			return false;
		}
	}

	plugins_.push_back(std::make_unique<Plugin>(Plugin{info}));
	Plugin &plugin = *plugins_.back();
	const auto remembered = enabled_.find(info->id);
	plugin.enabled = remembered != enabled_.end() && remembered->second;

	std::vector<std::string> problems;
	router_.add(plugin, problems);
	for (const std::string &problem : problems)
		blog(LOG_WARNING, "%s %s: chat command left out: %s", kLog, info->name, problem.c_str());

	blog(LOG_INFO, "%s %s registered: %s", kLog, info->name, plugin.enabled ? "added" : "not added");
	info->set_enabled(info->data, plugin.enabled);
	emit pluginRegistered(&plugin);
	return true;
}

void Manager::setEnabled(Plugin &plugin, bool enabled)
{
	if (closed_ || plugin.enabled == enabled)
		return;
	plugin.enabled = enabled;
	enabled_[plugin.info->id] = enabled;
	save();
	blog(LOG_INFO, "%s %s %s", kLog, plugin.info->name, enabled ? "added" : "removed");
	plugin.info->set_enabled(plugin.info->data, enabled);
	emit pluginChanged(&plugin);
}

void Manager::settingsChanged(const char *id)
{
	for (const auto &plugin : plugins_)
		if (id && std::strcmp(plugin->info->id, id) == 0)
			emit pluginChanged(plugin.get());
}

// ---- chat -----------------------------------------------------------------------------------------------------------

void Manager::followTwitchAccount(const QString &account)
{
	if (closed_)
		return;
	if (!TwitchChat::validChannel(account))
		twitch_.leave();
	else if (twitch_.state() == TwitchChat::State::Off || twitch_.channel() != account.toLower())
		twitch_.join(account);
}

QString twitchAccount(config_t *profile)
{
	if (!profile)
		return {};
	const char *type = config_get_string(profile, "Auth", "Type");
	const char *name = config_get_string(profile, "Twitch", "Name");
	const QString account = QString::fromUtf8(name ? name : "");
	return type && std::strcmp(type, "Twitch") == 0 && TwitchChat::validChannel(account) ? account : QString();
}

void Manager::route(const ChatMessage &message)
{
	if (closed_)
		return;
	router_.route(message, clock_.nsecsElapsed() / 1e6);
}

// ---- VTube Studio ---------------------------------------------------------------------------------------------------

void Manager::connectVts(int port)
{
	vtsPort_ = port;
	vtsOn_ = true;
	save();
	vts_.connectTo(port, vtsToken_, true);
}

void Manager::disconnectVts()
{
	vtsOn_ = false;
	save();
	vts_.disconnect();
}

void Manager::vtsRequest(const sarxina_vts_request &request)
{
	if (closed_ || !request.type || !request.done)
		return;
	const auto done = request.done;
	void *param = request.param;
	const auto answer = [this, done, param](bool ok, const QJsonObject &data) {
		if (!closed_)
			done(param, ok, QJsonDocument(data).toJson(QJsonDocument::Compact).constData());
	};

	QJsonObject data;
	if (request.data && *request.data) {
		const QJsonDocument document = QJsonDocument::fromJson(request.data);
		if (!document.isObject()) {
			QTimer::singleShot(0, this, [answer] {
				answer(false, {{QStringLiteral("message"), QStringLiteral("the request's data is not a JSON object")}});
			});
			return;
		}
		data = document.object();
	}
	vts_.request(QString::fromUtf8(request.type), data, answer);
}

// ---- starting, stopping, remembering --------------------------------------------------------------------------------

void Manager::start()
{
	if (vtsOn_ && !vtsToken_.isEmpty()) // never asks VTube Studio for a new token on its own
		vts_.connectTo(vtsPort_, vtsToken_, false);
}

void Manager::shutDown()
{
	closed_ = true;
	twitch_.leave();
	vts_.disconnect();
}

void Manager::load()
{
	if (use_ != Use::Obs)
		return;
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!path || !os_file_exists(path))
		return;
	OBSDataAutoRelease config = obs_data_create_from_json_file_safe(path, "bak");
	if (!config)
		return;
	obs_data_set_default_int(config, "vts_port", 8001);

	vtsPort_ = static_cast<int>(obs_data_get_int(config, "vts_port"));
	vtsOn_ = obs_data_get_bool(config, "vts_on");
	vtsToken_ = QString::fromUtf8(obs_data_get_string(config, "vts_token"));

	OBSDataAutoRelease plugins = obs_data_get_obj(config, "plugins");
	for (obs_data_item_t *item = plugins ? obs_data_first(plugins) : nullptr; item; obs_data_item_next(&item))
		enabled_[obs_data_item_get_name(item)] = obs_data_item_get_bool(item);
}

void Manager::save() const
{
	if (use_ != Use::Obs)
		return;
	BPtr<char> folder(obs_module_config_path(""));
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!folder || !path)
		return;
	os_mkdirs(folder);

	OBSDataAutoRelease plugins = obs_data_create();
	for (const auto &[id, enabled] : enabled_)
		obs_data_set_bool(plugins, id.c_str(), enabled);
	OBSDataAutoRelease config = obs_data_create();
	obs_data_set_obj(config, "plugins", plugins);
	obs_data_set_int(config, "vts_port", vtsPort_);
	obs_data_set_bool(config, "vts_on", vtsOn_);
	obs_data_set_string(config, "vts_token", vtsToken_.toUtf8().constData());
	obs_data_save_json_safe(config, path, "tmp", "bak");
}

} // namespace sarxina
