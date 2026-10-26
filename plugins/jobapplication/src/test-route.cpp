// A way to hand JobApplication chat messages without Twitch, for a demo or a test: obs-websocket's vendor request
// "chat" of the vendor "JobApplication" (CallVendorRequest), with
//   {"user": "a name", "text": "what they wrote", "reward_id": "the reward's id, for a redeem; leave it out for chat"}
// The message goes the way chat from the Sarxina OBS Plugin Manager goes, through the trigger and its checks: with the
// reward trigger it needs the id of the reward picked in the settings, with the command trigger "!jobapp ..." as the
// text. It answers {"accepted": true} once the message is on its way.
#include "jobapplication.hpp"

#include "test-route.hpp"

#include <obs-module.h>

#include <QMetaObject>
#include <QPointer>

namespace sarxina::jobapplication {

namespace {

void chatRequest(obs_data_t *request, obs_data_t *response, void *data)
{
	// On one of obs-websocket's threads: over to the UI thread, where the plugin lives.
	QPointer<JobApplication> plugin = static_cast<JobApplication *>(data);
	const TestChat chat = TestChat::fromRequest(request);
	QMetaObject::invokeMethod(
		plugin.data(),
		[plugin, chat] {
			if (plugin)
				plugin->chat(chat.message());
		},
		Qt::QueuedConnection);
	obs_data_set_bool(response, "accepted", true);
}

} // namespace

void addTestRoute(JobApplication &plugin)
{
	const bool ok = addVendorRequest("JobApplication", "chat", chatRequest, &plugin);
	blog(ok ? LOG_INFO : LOG_WARNING, "[%s] test route %s: obs-websocket's vendor request \"chat\" of \"JobApplication\"",
	     PLUGIN_TITLE, ok ? "ready" : "NOT added");
}

} // namespace sarxina::jobapplication
