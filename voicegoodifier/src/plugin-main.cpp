// VoiceGoodifier as an OBS plugin: its audio filters (see filters.hpp), and its self-test when OBS is started with
// VOICEGOODIFIER_SELFTEST=1.
#include "filters.hpp"
#include "in-obs-test.hpp"
#include "self-test.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QByteArray>
#include <QtGlobal>

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

namespace {

// Runs the self-test once OBS has loaded everything, when asked to.
void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING || qEnvironmentVariableIntValue("VOICEGOODIFIER_SELFTEST") == 0)
		return;
	const bool ok = runSelfTest(qgetenv("VOICEGOODIFIER_SELFTEST_PICTURES").constData()) &
			sarxina::voice::test::inObsTest();
	sarxina::test::report(ok, ok ? "passed, in OBS too" : "FAILED");
}

} // namespace

bool obs_module_load(void)
{
	sarxina::voice::registerFilters();
	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	blog(LOG_INFO, "[%s] %s loaded", PLUGIN_TITLE, PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
