// Ways to work KeepTalking without a mic, for a demo or a test: obs-websocket's vendor requests of the vendor
// "KeepTalking" (CallVendorRequest).
//   "start"   starts the countdown now, as if the quiet had lasted long enough; at zero the game picked is closed.
//             Only while OBS streams.
//   "talk"    counts as talking now: stops a countdown.
//   "status"  what it is doing: on, game, game_running, mic, mic_found, level_db, threshold_db, seconds, and countdown
//             (the number on screen, -1 for none), streaming, words (this stream's, or the last one's) and
//             stream_seconds, and the last stream sent to the leaderboard: leaderboard_sent and leaderboard (what came
//             of it, as the settings show it: "Sent: Long Stream, rank 3", "Not sent: ...", "Sending...").
//   "submit_test" {"words": 1234, "seconds": 600.5, "started": "2026-11-22T20:00:00Z"} sends that stream to the
//             leaderboard now, as if it had just ended, whatever the setting and its length (the service judges it).
//             started may be left out: then it is now minus seconds. "login" (optional) sends it as that channel
//             without asking Twitch. Its result shows in "status".
// "start", "talk" and "submit_test" answer {"accepted": true} once they are on their way.
#include "keeptalking.hpp"

#include "test-route.hpp"

#include <obs-module.h>

#include <QDateTime>
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

void submitTestRequest(obs_data_t *request, obs_data_t *response, void *data)
{
	const long long words = request ? obs_data_get_int(request, "words") : 0;
	const double seconds = request ? obs_data_get_double(request, "seconds") : 0;
	const char *startedText = request ? obs_data_get_string(request, "started") : "";
	QDateTime started = QDateTime::fromString(QString::fromUtf8(startedText ? startedText : ""), Qt::ISODate);
	if (!started.isValid())
		started = QDateTime::currentDateTimeUtc().addMSecs(-static_cast<qint64>(seconds * 1000));
	started = started.toUTC();
	const char *loginText = request ? obs_data_get_string(request, "login") : "";
	const QString login = QString::fromUtf8(loginText ? loginText : "");
	onUiThread(data, [words, seconds, started, login](KeepTalking &plugin) {
		plugin.submitTest(words, seconds, started, login);
	});
	obs_data_set_bool(response, "accepted", true);
	obs_data_set_string(response, "started", started.toString(Qt::ISODate).toUtf8().constData());
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
	obs_data_set_bool(response, "streaming", status.streaming);
	obs_data_set_int(response, "words", status.words);
	obs_data_set_double(response, "stream_seconds", status.streamSeconds);
	obs_data_set_bool(response, "leaderboard_sent", status.leaderboardSent);
	obs_data_set_string(response, "leaderboard", status.leaderboard.c_str());
}

} // namespace

void addTestRoute(KeepTalking &plugin)
{
	const bool start = addVendorRequest("KeepTalking", "start", startRequest, &plugin);
	const bool talk = addVendorRequest("KeepTalking", "talk", talkRequest, &plugin);
	const bool status = addVendorRequest("KeepTalking", "status", statusRequest, &plugin);
	const bool submit = addVendorRequest("KeepTalking", "submit_test", submitTestRequest, &plugin);
	const bool ok = start && talk && status && submit;
	blog(ok ? LOG_INFO : LOG_WARNING,
	     "[%s] test route %s: obs-websocket's vendor requests \"start\", \"talk\", \"status\" and \"submit_test\" of \"KeepTalking\"",
	     PLUGIN_TITLE, ok ? "ready" : "NOT added");
}

} // namespace sarxina::keeptalking
