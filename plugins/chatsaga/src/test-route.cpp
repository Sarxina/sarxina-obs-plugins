// Ways to hand ChatSaga chat without Twitch, for a demo or a test: obs-websocket's vendor requests of the vendor
// "ChatSaga" (CallVendorRequest).
//   "chat"      {"user": "a name", "text": "what they wrote", "role": "broadcaster" or left out}: a chat message, handed
//               over the way the Sarxina OBS Plugin Manager hands chat over.
//   "chatters"  {"chatters": [{"name": "a name"}, ...]}: stands in for Twitch's list of everyone in chat; Twitch is
//               no longer asked until OBS restarts.
// Each answers {"accepted": true} once it is on its way.
#include "chatsaga.hpp"

#include "test-route.hpp"

#include <obs-module.h>

#include <QMetaObject>
#include <QPointer>
#include <QString>

namespace sarxina::chatsaga {

namespace {

// On one of obs-websocket's threads: `then` runs on the UI thread, where the plugin lives.
template<typename Then> void onUiThread(void *data, Then then)
{
	QPointer<ChatSaga> plugin = static_cast<ChatSaga *>(data);
	QMetaObject::invokeMethod(
		plugin.data(),
		[plugin, then] {
			if (plugin)
				then(*plugin);
		},
		Qt::QueuedConnection);
}

void chatRequest(obs_data_t *request, obs_data_t *response, void *data)
{
	const TestChat chat = TestChat::fromRequest(request);
	onUiThread(data, [chat](ChatSaga &plugin) { plugin.chat(chat.message()); });
	obs_data_set_bool(response, "accepted", true);
}

std::vector<Chatter> chattersIn(obs_data_t *request)
{
	std::vector<Chatter> chatters;
	obs_data_array_t *names = obs_data_get_array(request, "chatters");
	for (size_t i = 0; names && i < obs_data_array_count(names); i++) {
		obs_data_t *item = obs_data_array_item(names, i);
		const QString name = QString::fromUtf8(obs_data_get_string(item, "name"));
		obs_data_release(item);
		if (!name.isEmpty())
			chatters.push_back({name.toLower(), name, QStringLiteral("test-") + name.toLower()});
	}
	obs_data_array_release(names);
	return chatters;
}

void chattersRequest(obs_data_t *request, obs_data_t *response, void *data)
{
	const std::vector<Chatter> chatters = chattersIn(request);
	onUiThread(data, [chatters](ChatSaga &plugin) { plugin.listChattersForTest(chatters); });
	obs_data_set_bool(response, "accepted", true);
}

} // namespace

void addTestRoute(ChatSaga &plugin)
{
	const bool chat = addVendorRequest("ChatSaga", "chat", chatRequest, &plugin);
	const bool chatters = addVendorRequest("ChatSaga", "chatters", chattersRequest, &plugin);
	blog(chat && chatters ? LOG_INFO : LOG_WARNING,
	     "[%s] test route %s: obs-websocket's vendor requests \"chat\" and \"chatters\" of \"ChatSaga\"", PLUGIN_TITLE,
	     chat && chatters ? "ready" : "NOT added");
}

} // namespace sarxina::chatsaga
