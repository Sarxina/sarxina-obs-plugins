#include "jobapplication.hpp"

#include "paper-item.hpp"
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

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
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

// The button in the settings. Nothing in them changes, so the manager need not draw them again.
bool clearPressed(obs_properties_t *, obs_property_t *, void *data)
{
	self(data)->clear();
	return false;
}

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

JobApplication::JobApplication(PaperProp::Send send) : form_(jobApplication()), prop_(std::move(send))
{
	clock_.start();
	if (obs_initialized())
		load();
	prop_.said = [](const QString &line) { blog(LOG_INFO, "[%s] %s", PLUGIN_TITLE, utf8(line).constData()); };
	prop_.wrote = [this](int writing) { wrote(writing); };
	trigger_.changed = [this] { settingsChanged(); };
	pump_.setInterval(kPumpMs);
	connect(&pump_, &QTimer::timeout, this, &JobApplication::pump);
	check_.setInterval(kCheckMs);
	connect(&check_, &QTimer::timeout, this, [this] { prop_.check(); });
	writeLater_.setSingleShot(true);
	connect(&writeLater_, &QTimer::timeout, this, &JobApplication::write);
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

void JobApplication::runForTest(int gatherMs, int writeEveryMs)
{
	testing_ = true;
	gatherMs_ = gatherMs;
	writeEveryMs_ = writeEveryMs;
	managed_ = true;
	enabled_ = true;
}

void JobApplication::shutDown()
{
	save();
	closed_ = true;
	pump_.stop();
	check_.stop();
	writeLater_.stop();
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
		trigger_.listRewards(true); // so that a redeem of a reward the manager made can be marked fulfilled
	} else {
		prop_.hide();
		pump_.stop();
		check_.stop();
		writeLater_.stop();
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
	obs_properties_add_button2(properties, "clear", obs_module_text("Clear"), clearPressed, this);
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

// The trigger changed by itself (a reward made with the button) or has something new to show: kept, and drawn again
// once the settings are done with what they are doing.
void JobApplication::settingsChanged()
{
	save();
	if (obs_initialized())
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
	if (closed_ || testing_ || !obs_initialized())
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

// ---- chat writes on it ----------------------------------------------------------------------------------------------

void JobApplication::chat(const sarxina_chat_message &message)
{
	if (closed_ || !enabled_)
		return;
	Heard heard;
	if (trigger_.hear(message, now(), ready(), heard))
		took(heard);
	if (trigger_.rewardsChanged() && !trigger_.listed()) // a new one to pick from, if the settings are open
		settingsChanged();
}

void JobApplication::pump()
{
	Heard heard;
	while (trigger_.waiting() && trigger_.next(now(), ready(), heard))
		took(heard);
}

void JobApplication::took(const Heard &heard)
{
	if (!managed_) {
		blog(LOG_WARNING, "[%s] the job application needs the Sarxina OBS Plugin Manager, which reaches VTube Studio",
		     PLUGIN_TITLE);
		return;
	}
	const QString line = writable(heard.text, kLongestEntry);
	const QString by = writable(heard.who, kLongestSigned);
	if (line.isEmpty()) {
		blog(LOG_INFO, "[%s] %s wrote nothing that can go on the job application", PLUGIN_TITLE,
		     utf8(by).constData());
		return;
	}
	entries_.push_front({line, by});
	if (entries_.size() > kEntriesKept)
		entries_.pop_back();
	if (!heard.rewardId.empty())
		toFulfil_.emplace_back(painted_ + 1, heard); // the next painting has its line
	dirty_ = true;
	blog(LOG_INFO, "[%s] %s added to the job application's experience (%lld characters)", PLUGIN_TITLE,
	     utf8(by).constData(), static_cast<long long>(line.size()));
	if (!prop_.wanted()) { // down: up at once, with the line on it
		writeLater_.stop();
		paint();
		prop_.show();
		return;
	}
	writeSoon();
}

void JobApplication::clear()
{
	if (closed_)
		return;
	if (entries_.empty()) {
		blog(LOG_INFO, "[%s] the job application is blank already", PLUGIN_TITLE);
		return;
	}
	const size_t lines = entries_.size();
	entries_.clear();
	dirty_ = true;
	blog(LOG_INFO, "[%s] the job application cleared: %lld line%s of experience taken off", PLUGIN_TITLE,
	     static_cast<long long>(lines), lines == 1 ? "" : "s");
	if (enabled_ && prop_.wanted()) // up: written blank like new writing; down: painted blank when it comes up
		writeSoon();
}

// Lines that come close together are gathered and written together, and the paper is written at most every couple of
// seconds, so a burst of chat is one new paper, not one for each line.
void JobApplication::writeSoon()
{
	if (writeLater_.isActive())
		return; // the write coming takes this line too
	const double since = now() - lastWrite_;
	writeLater_.start(static_cast<int>(std::max<double>(gatherMs_, writeEveryMs_ - since)));
}

void JobApplication::write()
{
	if (closed_ || !enabled_)
		return;
	paint();
	prop_.rewrite();
}

void JobApplication::wrote(int writing)
{
	// Each redeem whose line is on the paper on screen now is done.
	auto done = std::stable_partition(toFulfil_.begin(), toFulfil_.end(),
					  [writing](const auto &waiting) { return waiting.first > writing; });
	for (auto at = done; at != toFulfil_.end(); ++at)
		trigger_.fulfil(at->second);
	toFulfil_.erase(done, toFulfil_.end());
}

// ---- the hotkey -----------------------------------------------------------------------------------------------------

void JobApplication::toggle(const QString &who)
{
	if (closed_ || !enabled_)
		return;
	if (!managed_) {
		blog(LOG_WARNING, "[%s] the job application needs the Sarxina OBS Plugin Manager, which reaches VTube Studio",
		     PLUGIN_TITLE);
		return;
	}
	writeLater_.stop(); // it comes up with every line so far, or goes
	paint();
	blog(LOG_INFO, "[%s] %s: the job application %s", PLUGIN_TITLE, utf8(who).constData(),
	     prop_.wanted() ? "goes" : "comes up");
	prop_.toggle();
}

void JobApplication::paint()
{
	if (!dirty_ && prop_.hasPicture())
		return;
	QElapsedTimer took;
	took.start();
	form_.set("name", text("Form.ChatName"));
	if (Field *experience = form_.field("experience"))
		experience->bullets.assign(entries_.begin(), entries_.end());
	const QImage sheet = paintSheet(form_, kPaperSeed);
	const QByteArray texture = png(paperTexture(sheet), true); // for the Live2D item that bends
	prop_.setTexture(texture);
	prop_.setPicture([sheet] { return png(paintPicture(sheet)); }); // the flat picture, only if it is needed
	prop_.writing = ++painted_;
	dirty_ = false;
	lastWrite_ = now();
	blog(LOG_INFO, "[%s] the paper painted with %lld line%s of experience: %lld KB as the Live2D item's texture, in %lld ms",
	     PLUGIN_TITLE, static_cast<long long>(entries_.size()), entries_.size() == 1 ? "" : "s",
	     static_cast<long long>(texture.size() / 1024), static_cast<long long>(took.elapsed()));
}

} // namespace sarxina::jobapplication
