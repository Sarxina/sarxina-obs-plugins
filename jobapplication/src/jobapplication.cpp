#include "jobapplication.hpp"

#include "paper.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>

#include <QJsonDocument>
#include <QMetaObject>

#include <algorithm>
#include <memory>

namespace sarxina::jobapplication {

namespace {

constexpr int kPumpMs = 250;   // how often a redeem that waits is looked at
constexpr int kCheckMs = 3000; // how often VTube Studio is asked whether the paper is still up
constexpr const char *kHotkeyName = "jobapplication.toggle";

JobApplication *self(void *data)
{
	return static_cast<JobApplication *>(data);
}

QByteArray utf8(const QString &text)
{
	return text.toUtf8();
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

// On OBS's hotkey thread: over to the UI thread, where everything else happens.
void hotkeyPressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	JobApplication *plugin = self(data);
	QMetaObject::invokeMethod(
		plugin, [plugin] { plugin->toggle(QString::fromUtf8(obs_module_text("Hotkey.Who"))); },
		Qt::QueuedConnection);
}

} // namespace

// Its JSON goes as text both ways.
bool sendToVts(const QString &type, const QJsonObject &data, PaperProp::Answer answer)
{
	struct Pending {
		PaperProp::Answer answer;
	};
	const QByteArray name = type.toUtf8();
	const QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact);
	auto *pending = new Pending{std::move(answer)};
	const sarxina_vts_request request{name.constData(), json.constData(),
					  [](void *param, bool ok, const char *answerJson) {
						  std::unique_ptr<Pending> waiting(static_cast<Pending *>(param));
						  waiting->answer(ok, QJsonDocument::fromJson(answerJson ? answerJson : "{}")
									      .object());
					  },
					  pending};
	if (!sarxina_obs_plugin_manager_vts_request(&request)) {
		delete pending;
		return false;
	}
	return true;
}

JobApplication::JobApplication() : form_(jobApplication()), prop_(sendToVts)
{
	clock_.start();
	load();
	prop_.said = [](const QString &line) { blog(LOG_INFO, "[%s] %s", PLUGIN_TITLE, utf8(line).constData()); };
	pump_.setInterval(kPumpMs);
	connect(&pump_, &QTimer::timeout, this, &JobApplication::pump);
	check_.setInterval(kCheckMs);
	connect(&check_, &QTimer::timeout, this, [this] { prop_.check(); });
}

void JobApplication::start()
{
	hotkey_ = obs_hotkey_register_frontend(kHotkeyName, obs_module_text("Hotkey"), hotkeyPressed, this);
	if (savedKeys_) {
		obs_hotkey_load(hotkey_, savedKeys_);
		obs_data_array_release(savedKeys_);
		savedKeys_ = nullptr;
	}

	info.description = obs_module_text("Description");
	info.data = this;
	if (sarxina_obs_plugin_manager_register(&info)) {
		blog(LOG_INFO, "[%s] %s loaded, run by the Sarxina OBS Plugin Manager", PLUGIN_TITLE, PLUGIN_VERSION);
	} else {
		// No chat and no VTube Studio without it: the hotkey says so in the log.
		blog(LOG_WARNING, "[%s] %s loaded, but the Sarxina OBS Plugin Manager is not installed", PLUGIN_TITLE,
		     PLUGIN_VERSION);
		managed_ = false;
		enabled_ = true;
	}
}

void JobApplication::shutDown()
{
	save();
	closed_ = true;
	pump_.stop();
	check_.stop();
	prop_.stop(); // VTube Studio takes the paper down itself when the manager's connection closes
	if (hotkey_ != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(hotkey_);
		hotkey_ = OBS_INVALID_HOTKEY_ID;
	}
}

// ---- on and off -----------------------------------------------------------------------------------------------------

void JobApplication::setEnabled(bool enabled)
{
	managed_ = true; // only the manager calls this
	enabled_ = enabled;
	if (enabled) {
		pump_.start();
		check_.start();
		QTimer::singleShot(1000, this, [this] { paint(); }); // ready before the first toggle
	} else {
		prop_.hide();
		pump_.stop();
		check_.stop();
	}
	blog(LOG_INFO, "[%s] %s", PLUGIN_TITLE, enabled ? "on" : "off");
}

// ---- settings -------------------------------------------------------------------------------------------------------

obs_properties_t *JobApplication::properties()
{
	obs_properties_t *properties = obs_properties_create();
	addTriggerProperties(properties, trigger_, false);
	obs_property_t *size = obs_properties_add_int(properties, "size", obs_module_text("Size"), kLeastSize, kMostSize, 1);
	obs_property_int_set_suffix(size, " %");
	obs_property_set_long_description(size, obs_module_text("Size.Help"));
	return properties;
}

void JobApplication::getSettings(obs_data_t *settings) const
{
	triggerToSettings(settings, trigger_.settings());
	obs_data_set_int(settings, "size", sizePercent_);
}

void JobApplication::update(obs_data_t *settings)
{
	bool rewrite = false;
	trigger_.setSettings(triggerFromSettings(settings, trigger_, rewrite));
	const int size = std::clamp(static_cast<int>(obs_data_get_int(settings, "size")), kLeastSize, kMostSize);
	if (size != sizePercent_) {
		sizePercent_ = size;
		prop_.setSize(sizePercent_ / 100.0);
		if (prop_.shown()) { // up again at the new size, to see it
			prop_.hide();
			prop_.show();
		}
	}
	save();
	if (rewrite) // once the settings are done with this change
		QTimer::singleShot(0, this, [] { sarxina_obs_plugin_manager_settings_changed(PLUGIN_NAME); });
}

void JobApplication::load()
{
	OBSDataAutoRelease config;
	BPtr<char> path(obs_module_config_path("config.json"));
	if (path && os_file_exists(path))
		config = obs_data_create_from_json_file_safe(path, "bak");
	trigger_.setSettings(loadTrigger(config, trigger_.settings()));
	if (config) {
		obs_data_set_default_int(config, "size", kDefaultSize);
		sizePercent_ = std::clamp(static_cast<int>(obs_data_get_int(config, "size")), kLeastSize, kMostSize);
		savedKeys_ = obs_data_get_array(config, "hotkey");
	}
	prop_.setSize(sizePercent_ / 100.0);
}

void JobApplication::save() const
{
	if (closed_)
		return;
	BPtr<char> folder(obs_module_config_path(""));
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!folder || !path)
		return;
	os_mkdirs(folder);
	OBSDataAutoRelease config = obs_data_create();
	saveTrigger(config, trigger_.settings());
	obs_data_set_int(config, "size", sizePercent_);
	if (hotkey_ != OBS_INVALID_HOTKEY_ID) {
		OBSDataArrayAutoRelease keys = obs_hotkey_save(hotkey_);
		obs_data_set_array(config, "hotkey", keys);
	}
	obs_data_save_json_safe(config, path, "tmp", "bak");
}

// ---- chat and the toggle --------------------------------------------------------------------------------------------

void JobApplication::chat(const sarxina_chat_message &message)
{
	if (closed_ || !enabled_)
		return;
	QString who;
	if (trigger_.hear(message, now(), ready(), who))
		toggle(who);
	if (trigger_.rewardsChanged()) // a new one to pick from, if the settings are open
		QTimer::singleShot(0, this, [] { sarxina_obs_plugin_manager_settings_changed(PLUGIN_NAME); });
}

void JobApplication::pump()
{
	QString who;
	if (trigger_.waiting() && trigger_.next(now(), ready(), who))
		toggle(who);
}

void JobApplication::toggle(const QString &who)
{
	if (closed_ || !enabled_)
		return;
	if (!managed_) {
		blog(LOG_WARNING, "[%s] the job application needs the Sarxina OBS Plugin Manager, which reaches VTube Studio",
		     PLUGIN_TITLE);
		return;
	}
	paint();
	blog(LOG_INFO, "[%s] %s: the job application %s", PLUGIN_TITLE, utf8(who).constData(),
	     prop_.wanted() ? "goes" : "comes up");
	prop_.toggle();
}

void JobApplication::paint()
{
	if (prop_.hasPicture())
		return;
	QElapsedTimer took;
	took.start();
	const QByteArray picture = png(paintPicture(form_, kPaperSeed));
	prop_.setPicture(picture);
	blog(LOG_INFO, "[%s] the paper painted: %lld KB, in %lld ms", PLUGIN_TITLE,
	     static_cast<long long>(picture.size() / 1024), static_cast<long long>(took.elapsed()));
}

} // namespace sarxina::jobapplication
