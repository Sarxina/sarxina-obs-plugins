// A way to hand JobApplication chat messages without Twitch, for a demo or a test: obs-websocket's vendor request
// "chat" of the vendor "JobApplication" (CallVendorRequest), with
//   {"user": "a name", "text": "what they wrote", "reward_id": "the reward's id, for a redeem; leave it out for chat"}
// The message goes the way chat from the Sarxina OBS Plugin Manager goes, through the trigger and its checks: with the
// reward trigger it needs the id of the reward picked in the settings, with the command trigger "!jobapp ..." as the
// text. It answers {"accepted": true} once the message is on its way.
//
// obs-websocket's vendor API is a few calls on its own procedure handler (its lib/obs-websocket-api.h), written out here
// so that nothing of obs-websocket is needed to build. Without obs-websocket there is simply no test route.
#include "jobapplication.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <string>

namespace sarxina::jobapplication {

namespace {

using RequestCallback = void (*)(obs_data_t *request, obs_data_t *response, void *data);
struct Callback { // obs-websocket's struct obs_websocket_request_callback
	RequestCallback callback;
	void *data;
};

// A message as the manager hands one over: no control characters, at most 500 characters.
std::string chatText(const char *text)
{
	QString clean;
	for (const char32_t c : QString::fromUtf8(text ? text : "").toUcs4())
		if (c >= 0x20 && !(c >= 0x7F && c <= 0x9F))
			clean += QString::fromUcs4(&c, 1);
	return clean.left(500).toStdString();
}

void chatRequest(obs_data_t *request, obs_data_t *response, void *data)
{
	// On one of obs-websocket's threads: over to the UI thread, where the plugin lives.
	QPointer<JobApplication> plugin = static_cast<JobApplication *>(data);
	const std::string user = chatText(obs_data_get_string(request, "user"));
	const std::string text = chatText(obs_data_get_string(request, "text"));
	const std::string reward = chatText(obs_data_get_string(request, "reward_id"));
	QMetaObject::invokeMethod(
		plugin.data(),
		[plugin, user, text, reward] {
			if (!plugin)
				return;
			const std::string login = QString::fromStdString(user).toLower().toStdString();
			const std::string id = "test-" + login;
			sarxina_chat_message message{};
			message.platform = "twitch";
			message.id = "test";
			message.user_id = id.c_str();
			message.user_name = login.c_str();
			message.display_name = user.c_str();
			message.role = SARXINA_ROLE_EVERYONE;
			message.text = text.c_str();
			message.time_ms = QDateTime::currentMSecsSinceEpoch();
			message.reward_id = reward.c_str();
			plugin->chat(message);
		},
		Qt::QueuedConnection);
	obs_data_set_bool(response, "accepted", true);
}

} // namespace

void addTestRoute(JobApplication &plugin)
{
	proc_handler_t *global = obs_get_proc_handler();
	calldata_t call = {};
	if (!global || !proc_handler_call(global, "obs_websocket_api_get_ph", &call)) {
		calldata_free(&call);
		return; // no obs-websocket
	}
	auto *websocket = static_cast<proc_handler_t *>(calldata_ptr(&call, "ph"));
	calldata_free(&call);
	if (!websocket)
		return;

	calldata_t vendor = {};
	calldata_set_string(&vendor, "name", "JobApplication");
	proc_handler_call(websocket, "vendor_register", &vendor);
	void *registered = calldata_ptr(&vendor, "vendor");
	calldata_free(&vendor);
	if (!registered)
		return;

	static Callback chat; // obs-websocket keeps a copy; this one lasts anyway
	chat = {chatRequest, &plugin};
	calldata_t request = {};
	calldata_set_ptr(&request, "vendor", registered);
	calldata_set_string(&request, "type", "chat");
	calldata_set_ptr(&request, "callback", &chat);
	proc_handler_call(websocket, "vendor_request_register", &request);
	const bool ok = calldata_bool(&request, "success");
	calldata_free(&request);
	blog(ok ? LOG_INFO : LOG_WARNING, "[%s] test route %s: obs-websocket's vendor request \"chat\" of \"JobApplication\"",
	     PLUGIN_TITLE, ok ? "ready" : "NOT added");
}

} // namespace sarxina::jobapplication
