// Ways to work KeepTalking without a mic, for a demo or a test: obs-websocket's vendor requests of the vendor
// "KeepTalking" (CallVendorRequest).
//   "start"   starts the countdown now, as if the quiet had lasted long enough; at zero the game picked is closed.
//   "talk"    counts as talking now: stops a countdown.
//   "status"  what it is doing: on, game, game_running, mic, mic_found, level_db, threshold_db, seconds, and countdown
//             (the number on screen, -1 for none).
// "start" and "talk" answer {"accepted": true} once they are on their way.
#include "keeptalking.hpp"

#include "test-route.hpp"

#include <obs-module.h>

#include <QMetaObject>
#include <QPointer>

namespace sarxina::keeptalking {

void addTestRoute(KeepTalking &plugin);

namespace {

// On one of obs-websocket's threads: `then` runs on the UI thread, where the plugin lives.
template<typename Then> void onUiThread(void *data, Then then)
{
	QPointer<KeepTalking> plugin = static_cast<KeepTalking *>(data);
	QMetaObject::invokeMethod(
		plugin.data(),
		[plugin, then] {
			if (plugin)
				then(*plugin);
		},
		Qt::QueuedConnection);
}

void startRequest(obs_data_t *, obs_data_t *response, void *data)
{
	onUiThread(data, [](KeepTalking &plugin) { plugin.startCountdown(); });
	obs_data_set_bool(response, "accepted", true);
}

void talkRequest(obs_data_t *, obs_data_t *response, void *data)
{
	onUiThread(data, [](KeepTalking &plugin) { plugin.talk(); });
	obs_data_set_bool(response, "accepted", true);
}

void statusRequest(obs_data_t *, obs_data_t *response, void *data)
{
	const Status status = static_cast<KeepTalking *>(data)->status();
	obs_data_set_bool(response, "on", status.on);
	obs_data_set_string(response, "game", status.game.c_str());
	obs_data_set_bool(response, "game_running", status.gameRunning);
	obs_data_set_string(response, "mic", status.mic.c_str());
	obs_data_set_bool(response, "mic_found", status.micFound);
	obs_data_set_double(response, "level_db", status.levelDb);
	obs_data_set_double(response, "threshold_db", status.thresholdDb);
	obs_data_set_int(response, "seconds", status.seconds);
	obs_data_set_int(response, "countdown", status.shown);
}

} // namespace

void addTestRoute(KeepTalking &plugin)
{
	const bool start = addVendorRequest("KeepTalking", "start", startRequest, &plugin);
	const bool talk = addVendorRequest("KeepTalking", "talk", talkRequest, &plugin);
	const bool status = addVendorRequest("KeepTalking", "status", statusRequest, &plugin);
	const bool ok = start && talk && status;
	blog(ok ? LOG_INFO : LOG_WARNING,
	     "[%s] test route %s: obs-websocket's vendor requests \"start\", \"talk\" and \"status\" of \"KeepTalking\"",
	     PLUGIN_TITLE, ok ? "ready" : "NOT added");
}

} // namespace sarxina::keeptalking
