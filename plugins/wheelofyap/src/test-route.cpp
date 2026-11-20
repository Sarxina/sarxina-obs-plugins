// A way to spin the wheel without Twitch, for a demo or a test: obs-websocket's vendor requests of the vendor
// "WheelOfYap" (CallVendorRequest):
//   "spin"   {"user": "a name"}
//   "force"  {"user": "a name", "text": "the topic"}
// Each waits its turn like a redeem, and answers {"accepted": true} once it is on its way.
#include "wheelofyap.hpp"

#include "test-route.hpp"

#include <obs-module.h>

#include <QMetaObject>
#include <QPointer>

namespace sarxina::wheelofyap {

namespace {

void request(obs_data_t *request, obs_data_t *response, bool forced, void *data)
{
	// On one of obs-websocket's threads: over to the UI thread, where the plugin lives.
	QPointer<WheelOfYap> plugin = static_cast<WheelOfYap *>(data);
	const QString who = QString::fromUtf8(obs_data_get_string(request, "user"));
	const QString text = forced ? QString::fromUtf8(obs_data_get_string(request, "text")) : QString();
	QMetaObject::invokeMethod(
		plugin.data(),
		[plugin, who, text] {
			if (plugin)
				plugin->demo(who, text);
		},
		Qt::QueuedConnection);
	obs_data_set_bool(response, "accepted", true);
}

} // namespace

void addTestRoute(WheelOfYap &plugin)
{
	const bool spin = addVendorRequest(
		"WheelOfYap", "spin", [](obs_data_t *in, obs_data_t *out, void *data) { request(in, out, false, data); },
		&plugin);
	const bool force = addVendorRequest(
		"WheelOfYap", "force", [](obs_data_t *in, obs_data_t *out, void *data) { request(in, out, true, data); },
		&plugin);
	blog(spin && force ? LOG_INFO : LOG_WARNING,
	     "[%s] test route %s: obs-websocket's vendor requests \"spin\" and \"force\" of \"WheelOfYap\"", PLUGIN_TITLE,
	     spin && force ? "ready" : "NOT added");
}

} // namespace sarxina::wheelofyap
