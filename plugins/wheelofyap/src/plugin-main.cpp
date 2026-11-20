// WheelOfYap as an OBS plugin: the wheel as a source (wheel-source.hpp), registered when OBS loads the plugin so that
// scenes with one load, and the plugin itself (wheelofyap.hpp), handed to the Sarxina OBS Plugin Manager once every
// plugin has loaded, with the wheel in the manager's "Sarxina Source" menu.
//
// Threads: chat, the settings, the topics and the painting are on OBS's UI thread. Each wheel turns and is drawn on
// OBS's graphics thread, and hands back through OBS's UI task queue the topic it stopped on.
#include "self-test.hpp"
#include "wheel-source.hpp"
#include "wheelofyap.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/util.hpp>

#include <QFile>
#include <QTextStream>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return PLUGIN_TITLE;
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

namespace sarxina::wheelofyap {
void addTestRoute(WheelOfYap &plugin);
}

namespace {

sarxina::wheelofyap::WheelOfYap *plugin = nullptr;

// The topics a new wheel starts with: topics.txt in the plugin's data, one a line.
std::vector<sarxina::wheelofyap::Topic> defaultTopics()
{
	std::vector<sarxina::wheelofyap::Topic> topics;
	BPtr<char> path = obs_module_file("topics.txt");
	QFile file(QString::fromUtf8(path ? path.Get() : ""));
	if (!path || !file.open(QIODevice::ReadOnly | QIODevice::Text))
		return topics;
	QTextStream in(&file);
	in.setEncoding(QStringConverter::Utf8);
	while (!in.atEnd()) {
		const QString line = sarxina::wheelofyap::topicText(in.readLine());
		if (!line.isEmpty())
			topics.push_back({line, true});
	}
	return topics;
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (qEnvironmentVariableIntValue("WHEELOFYAP_SELFTEST") != 0)
			runSelfTest(qgetenv("WHEELOFYAP_SELFTEST_PICTURES").constData());
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
		plugin->shutDown();
	}
}

} // namespace

bool obs_module_load(void)
{
	sarxina::wheelofyap::registerWheelSource();
	return true;
}

void obs_module_post_load(void)
{
	plugin = new sarxina::wheelofyap::WheelOfYap(defaultTopics());
	sarxina::wheelofyap::setLandingHandler([](const sarxina::wheelofyap::Landed &landed) {
		if (plugin)
			plugin->landed(landed);
	});
	sarxina::wheelofyap::setEditTopicsHandler([] {
		if (plugin)
			plugin->editTopics();
	});
	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	plugin->start();
	const sarxina_source_offer offer{PLUGIN_NAME, sarxina::wheelofyap::kSourceId, obs_module_text("SourceName")};
	if (!sarxina_obs_plugin_manager_offer_source(&offer))
		blog(LOG_INFO, "[%s] the manager does not list sources (1.3.0 or newer does)", PLUGIN_TITLE);
	sarxina::wheelofyap::addTestRoute(*plugin);
}

void obs_module_unload(void)
{
	sarxina::wheelofyap::stopLandings();
	sarxina::wheelofyap::setLandingHandler(nullptr);
	sarxina::wheelofyap::setEditTopicsHandler(nullptr);
	sarxina::wheelofyap::releaseWheelGraphics();
	delete plugin;
	plugin = nullptr;
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
