#include "keeptalking.hpp"

#include "countdown-picture.hpp"
#include "countdown-source.hpp"
#include "processes.hpp"
#include "sarxina-obs-plugin-manager.h"

#include <obs-module.h>
#include <util/platform.h>
#include <util/util.hpp>

#include <QApplication>
#include <QFormLayout>
#include <QLabel>

#include <algorithm>
#include <cmath>

namespace sarxina::keeptalking {

namespace {

constexpr int kTickMs = 50;    // how often the countdown is stepped and the level shown
constexpr int kSlowMs = 1000;  // how often the game and the mic are looked for
constexpr float kSilentDb = -90; // under this, the level reads "silent"

KeepTalking *self(void *data)
{
	return static_cast<KeepTalking *>(data);
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
	nullptr, // no chat
	0,
	nullptr,
};

double seconds(uint64_t ns)
{
	return static_cast<double>(ns) / 1e9;
}

QString levelText(float db)
{
	if (!(db > kSilentDb))
		return QString::fromUtf8(obs_module_text("Level.Silent"));
	return QStringLiteral("%1 dB").arg(static_cast<int>(std::lround(db)));
}

} // namespace

KeepTalking::KeepTalking()
{
	load();
	meter_ = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_add_callback(meter_, levelArrived, this);
	tick_.setInterval(kTickMs);
	connect(&tick_, &QTimer::timeout, this, &KeepTalking::tick);
	slow_.setInterval(kSlowMs);
	connect(&slow_, &QTimer::timeout, this, &KeepTalking::slowTick);
	countdownFont(); // looked up here, on the UI thread, before the sources paint with it
}

KeepTalking::~KeepTalking()
{
	shutDown();
}

void KeepTalking::start()
{
	info.description = obs_module_text("Description");
	info.data = this;
	if (sarxina_obs_plugin_manager_register(&info)) {
		blog(LOG_INFO, "[%s] %s loaded, run by the Sarxina OBS Plugin Manager", PLUGIN_TITLE, PLUGIN_VERSION);
	} else {
		blog(LOG_INFO, "[%s] %s loaded, without the Sarxina OBS Plugin Manager (no settings to change)",
		     PLUGIN_TITLE, PLUGIN_VERSION);
		setEnabled(true);
		managed_ = false;
	}
}

void KeepTalking::shutDown()
{
	if (closed_)
		return;
	closed_ = true;
	tick_.stop();
	slow_.stop();
	showNumber(-1);
	if (meter_) {
		obs_volmeter_remove_callback(meter_, levelArrived, this);
		obs_volmeter_detach_source(meter_);
		obs_volmeter_destroy(meter_);
		meter_ = nullptr;
	}
	attached_ = nullptr;
}

void KeepTalking::setEnabled(bool enabled)
{
	managed_ = true; // only the manager calls this
	enabled_ = enabled && !closed_;
	if (enabled_) {
		tick_.start();
		slow_.start();
		slowTick();
	} else {
		tick_.stop();
		slow_.stop();
		detachMic();
		countdown_ = SilenceCountdown();
		shown_ = -1;
		showNumber(-1);
	}
	blog(LOG_INFO, "[%s] %s", PLUGIN_TITLE, enabled_ ? "on" : "off");
}

// ---- settings -------------------------------------------------------------------------------------------------------

obs_properties_t *KeepTalking::properties()
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_bool(properties, "on", obs_module_text("On"));

	obs_property_t *game = obs_properties_add_list(properties, "game", obs_module_text("Game"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	const std::vector<Program> programs = programsWithWindows();
	const bool listed = std::any_of(programs.begin(), programs.end(), [this](const Program &program) {
		return QString::fromStdString(program.exe).compare(QString::fromStdString(game_), Qt::CaseInsensitive) ==
		       0;
	});
	if (game_.empty())
		obs_property_list_add_string(game, obs_module_text("Game.None"), "");
	else if (!listed)
		obs_property_list_add_string(
			game, (game_ + " " + obs_module_text("Game.NotRunning")).c_str(), game_.c_str());
	for (const Program &program : programs) {
		// The exe as saved, so a game listed again after a restart is the same pick.
		const std::string value = QString::fromStdString(program.exe).compare(QString::fromStdString(game_),
										  Qt::CaseInsensitive) == 0
						  ? game_
						  : program.exe;
		obs_property_list_add_string(game, (program.exe + "  (" + program.title + ")").c_str(), value.c_str());
	}
	obs_properties_add_button2(
		properties, "refresh", obs_module_text("Refresh"),
		[](obs_properties_t *, obs_property_t *, void *) { return true; }, this);

	obs_property_t *mic = obs_properties_add_list(properties, "mic", obs_module_text("Mic"), OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_STRING);
	struct Mics {
		obs_property_t *list;
		const std::string &picked;
		bool found = false;
	} mics{mic, mic_};
	if (mic_.empty())
		obs_property_list_add_string(mic, obs_module_text("Mic.None"), "");
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto &mics = *static_cast<Mics *>(param);
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
				return true;
			const char *name = obs_source_get_name(source);
			if (!name || !*name)
				return true;
			mics.found = mics.found || mics.picked == name;
			obs_property_list_add_string(mics.list, name, name);
			return true;
		},
		&mics);
	if (!mic_.empty() && !mics.found)
		obs_property_list_insert_string(mic, 0, (mic_ + " " + obs_module_text("Mic.NotFound")).c_str(),
						mic_.c_str());

	obs_property_t *threshold =
		obs_properties_add_float(properties, "threshold", obs_module_text("Threshold"), -90, 0, 1);
	obs_property_float_set_suffix(threshold, " dB");
	obs_properties_add_text(properties, "level", obs_module_text("Level"), OBS_TEXT_DEFAULT);
	obs_property_t *quiet = obs_properties_add_int(properties, "seconds", obs_module_text("Seconds"), 1, 120, 1);
	obs_property_int_set_suffix(quiet, " s");
	levelField_.clear(); // drawn anew; found again on the next tick
	return properties;
}

void KeepTalking::getSettings(obs_data_t *settings) const
{
	obs_data_set_bool(settings, "on", on_);
	obs_data_set_string(settings, "game", game_.c_str());
	obs_data_set_string(settings, "mic", mic_.c_str());
	obs_data_set_double(settings, "threshold", thresholdDb_);
	obs_data_set_string(settings, "level", levelText(levelDb_).toUtf8().constData());
	obs_data_set_int(settings, "seconds", seconds_);
}

void KeepTalking::update(obs_data_t *settings)
{
	const bool wasOn = on_;
	const std::string game = obs_data_get_string(settings, "game");
	on_ = obs_data_get_bool(settings, "on");
	game_ = game;
	const std::string mic = obs_data_get_string(settings, "mic");
	const bool newMic = mic != mic_;
	mic_ = mic;
	thresholdDb_ = std::clamp(obs_data_get_double(settings, "threshold"), -90.0, 0.0);
	threshold_ = static_cast<float>(thresholdDb_);
	seconds_ = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, "seconds"), 1, 120));
	if (newMic)
		detachMic();
	if (wasOn != on_)
		blog(LOG_INFO, "[%s] %s: game %s, mic \"%s\", over %.0f dB, quiet for %d s", PLUGIN_TITLE,
		     on_ ? "watching" : "not watching", game_.empty() ? "none" : game_.c_str(), mic_.c_str(), thresholdDb_,
		     seconds_);
	save();
	if (enabled_)
		slowTick();
}

void KeepTalking::load()
{
	OBSDataAutoRelease config;
	BPtr<char> path(obs_module_config_path("config.json"));
	if (path && os_file_exists(path))
		config = obs_data_create_from_json_file_safe(path, "bak");
	if (!config)
		return;
	obs_data_set_default_double(config, "threshold", thresholdDb_);
	obs_data_set_default_int(config, "seconds", seconds_);
	on_ = obs_data_get_bool(config, "on");
	game_ = obs_data_get_string(config, "game");
	mic_ = obs_data_get_string(config, "mic");
	thresholdDb_ = std::clamp(obs_data_get_double(config, "threshold"), -90.0, 0.0);
	threshold_ = static_cast<float>(thresholdDb_);
	seconds_ = static_cast<int>(std::clamp<long long>(obs_data_get_int(config, "seconds"), 1, 120));
}

void KeepTalking::save() const
{
	BPtr<char> folder(obs_module_config_path(""));
	BPtr<char> path(obs_module_config_path("config.json"));
	if (!folder || !path)
		return;
	os_mkdirs(folder);
	OBSDataAutoRelease config = obs_data_create();
	obs_data_set_bool(config, "on", on_);
	obs_data_set_string(config, "game", game_.c_str());
	obs_data_set_string(config, "mic", mic_.c_str());
	obs_data_set_double(config, "threshold", thresholdDb_);
	obs_data_set_int(config, "seconds", seconds_);
	obs_data_save_json_safe(config, path, "tmp", "bak");
}

// ---- the mic --------------------------------------------------------------------------------------------------------

// On OBS's audio thread, for each bit of the mic's sound: its loudest channel, after its filters and volume.
void KeepTalking::levelArrived(void *param, const float[], const float peak[], const float[])
{
	auto *plugin = self(param);
	float loudest = -1e9f;
	for (int channel = 0; channel < MAX_AUDIO_CHANNELS; channel++)
		if (std::isfinite(peak[channel]))
			loudest = std::max(loudest, peak[channel]);
	plugin->levelDb_ = loudest;
	if (loudest >= plugin->threshold_.load())
		plugin->lastLoudNs_ = os_gettime_ns();
}

void KeepTalking::findMic()
{
	if (enabled_ && !closed_)
		attachMic();
}

void KeepTalking::attachMic()
{
	if (!meter_)
		return;
	OBSSourceAutoRelease source = mic_.empty() ? nullptr : obs_get_source_by_name(mic_.c_str());
	if (source && attached_ && obs_weak_source_references_source(attached_, source))
		return;
	if (!source && !attached_)
		return;
	detachMic();
	if (source && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) &&
	    obs_volmeter_attach_source(meter_, source)) {
		attached_ = obs_source_get_weak_source(source);
		blog(LOG_INFO, "[%s] listening to \"%s\"", PLUGIN_TITLE, mic_.c_str());
	}
}

void KeepTalking::detachMic()
{
	if (meter_ && attached_)
		obs_volmeter_detach_source(meter_);
	attached_ = nullptr;
	levelDb_ = -1e9f;
}

// ---- the countdown --------------------------------------------------------------------------------------------------

void KeepTalking::slowTick()
{
	if (closed_ || !enabled_)
		return;
	if (attached_ && obs_weak_source_expired(attached_)) // the mic was removed
		detachMic();
	attachMic();
	const bool running = !game_.empty() && isRunning(game_);
	if (running != gameRunning_ && !game_.empty())
		blog(LOG_INFO, "[%s] %s %s", PLUGIN_TITLE, game_.c_str(), running ? "is running" : "is not running");
	gameRunning_ = running;
}

void KeepTalking::tick()
{
	if (closed_ || !enabled_)
		return;
	const double now = seconds(os_gettime_ns());
	const uint64_t loudNs = lastLoudNs_.load();
	const double lastLoud = loudNs ? seconds(loudNs) : -1e9;
	const bool armed = on_ && attached_ && gameRunning_;

	switch (countdown_.step(now, armed, lastLoud, seconds_)) {
	case SilenceCountdown::Event::Started:
		blog(LOG_INFO, "[%s] quiet for %d s: counting down", PLUGIN_TITLE, seconds_);
		break;
	case SilenceCountdown::Event::Cancelled:
		blog(LOG_INFO, "[%s] countdown stopped", PLUGIN_TITLE);
		break;
	case SilenceCountdown::Event::Zero:
		closeGame();
		break;
	case SilenceCountdown::Event::None:
		break;
	}

	const int shown = countdown_.shown(now);
	if (shown != shown_) {
		shown_ = shown;
		showNumber(shown);
	}
	showLevel();

	std::lock_guard<std::mutex> hold(statusLock_);
	status_ = Status{on_,   game_, gameRunning_, mic_, static_cast<bool>(attached_), levelDb_.load(), thresholdDb_,
			 seconds_, shown_};
}

void KeepTalking::closeGame()
{
	if (game_.empty()) {
		blog(LOG_INFO, "[%s] zero, and no game picked", PLUGIN_TITLE);
		return;
	}
	if (!mayClose(game_)) {
		blog(LOG_WARNING, "[%s] zero, but %s is never closed", PLUGIN_TITLE, game_.c_str());
		return;
	}
	const Closed closed = closeProgram(game_);
	if (closed.found == 0)
		blog(LOG_INFO, "[%s] zero, and %s is not running", PLUGIN_TITLE, game_.c_str());
	else if (closed.closed == closed.found)
		blog(LOG_INFO, "[%s] zero: closed %s (%d process%s)", PLUGIN_TITLE, game_.c_str(), closed.closed,
		     closed.closed == 1 ? "" : "es");
	else
		blog(LOG_WARNING, "[%s] zero: closed %d of %d processes of %s; Windows said: %s", PLUGIN_TITLE,
		     closed.closed, closed.found, game_.c_str(), closed.problem.c_str());
	gameRunning_ = isRunning(game_);
}

// The level in the manager's settings, live while they are open: its field is found by its label the first time.
void KeepTalking::showLevel()
{
	static int sinceSearch = 0;
	if (!levelField_) {
		if (++sinceSearch < kSlowMs / kTickMs)
			return;
		sinceSearch = 0;
		const QString label = QString::fromUtf8(obs_module_text("Level"));
		for (QWidget *widget : QApplication::allWidgets()) {
			auto *text = qobject_cast<QLabel *>(widget);
			if (!text || text->text() != label || !text->parentWidget())
				continue;
			auto *form = qobject_cast<QFormLayout *>(text->parentWidget()->layout());
			int row = -1;
			QFormLayout::ItemRole role;
			if (!form || (form->getWidgetPosition(text, &row, &role), row < 0))
				continue;
			QLayoutItem *field = form->itemAt(row, QFormLayout::FieldRole);
			if (auto *edit = field ? qobject_cast<QLineEdit *>(field->widget()) : nullptr) {
				levelField_ = edit;
				edit->setReadOnly(true);
				break;
			}
		}
		if (!levelField_)
			return;
	}
	const QString text = levelText(levelDb_);
	if (levelField_->text() != text)
		levelField_->setText(text);
}

// ---- the test route -------------------------------------------------------------------------------------------------

void KeepTalking::startCountdown()
{
	if (closed_ || !enabled_)
		return;
	blog(LOG_INFO, "[%s] countdown started by the test route", PLUGIN_TITLE);
	countdown_.start(seconds(os_gettime_ns()));
	tick();
}

void KeepTalking::talk()
{
	lastLoudNs_ = os_gettime_ns();
}

Status KeepTalking::status() const
{
	std::lock_guard<std::mutex> hold(statusLock_);
	return status_;
}

} // namespace sarxina::keeptalking
