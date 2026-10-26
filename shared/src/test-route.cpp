#include "test-route.hpp"

#include <QDateTime>
#include <QString>

#include <deque>
#include <map>

namespace sarxina {

namespace {

struct Callback { // obs-websocket's struct obs_websocket_request_callback
	VendorRequest callback;
	void *data;
};

// obs-websocket's own procedure handler, or null without it.
proc_handler_t *websocketProcedures()
{
	proc_handler_t *global = obs_get_proc_handler();
	calldata_t call = {};
	proc_handler_t *websocket = nullptr;
	if (global && proc_handler_call(global, "obs_websocket_api_get_ph", &call))
		websocket = static_cast<proc_handler_t *>(calldata_ptr(&call, "ph"));
	calldata_free(&call);
	return websocket;
}

// The vendor of that name, registered the first time it is asked for; null if obs-websocket refused it.
void *vendor(proc_handler_t *websocket, const char *name)
{
	static std::map<std::string, void *> registered;
	auto known = registered.find(name);
	if (known != registered.end())
		return known->second;
	calldata_t call = {};
	calldata_set_string(&call, "name", name);
	proc_handler_call(websocket, "vendor_register", &call);
	void *made = calldata_ptr(&call, "vendor");
	calldata_free(&call);
	if (made)
		registered[name] = made;
	return made;
}

// Text as the manager hands chat over: no control characters, at most 500 characters.
std::string chatText(const char *text)
{
	QString clean;
	for (const char32_t c : QString::fromUtf8(text ? text : "").toUcs4())
		if (c >= 0x20 && !(c >= 0x7F && c <= 0x9F))
			clean += QString::fromUcs4(&c, 1);
	return clean.left(500).toStdString();
}

sarxina_role roleNamed(const std::string &name)
{
	static const std::map<std::string, sarxina_role> roles{{"subscriber", SARXINA_ROLE_SUBSCRIBER},
							       {"vip", SARXINA_ROLE_VIP},
							       {"moderator", SARXINA_ROLE_MODERATOR},
							       {"broadcaster", SARXINA_ROLE_BROADCASTER}};
	auto found = roles.find(name);
	return found == roles.end() ? SARXINA_ROLE_EVERYONE : found->second;
}

} // namespace

bool addVendorRequest(const char *vendorName, const char *type, VendorRequest request, void *data)
{
	proc_handler_t *websocket = websocketProcedures();
	void *registered = websocket ? vendor(websocket, vendorName) : nullptr;
	if (!registered)
		return false;
	static std::deque<Callback> callbacks; // obs-websocket keeps a copy; these last anyway
	callbacks.push_back({request, data});
	calldata_t call = {};
	calldata_set_ptr(&call, "vendor", registered);
	calldata_set_string(&call, "type", type);
	calldata_set_ptr(&call, "callback", &callbacks.back());
	proc_handler_call(websocket, "vendor_request_register", &call);
	const bool ok = calldata_bool(&call, "success");
	calldata_free(&call);
	return ok;
}

TestChat TestChat::fromRequest(obs_data_t *request)
{
	TestChat chat;
	chat.displayName = chatText(obs_data_get_string(request, "user"));
	chat.login = QString::fromStdString(chat.displayName).toLower().toStdString();
	chat.userId = "test-" + chat.login;
	chat.text = chatText(obs_data_get_string(request, "text"));
	chat.rewardId = chatText(obs_data_get_string(request, "reward_id"));
	chat.role = roleNamed(obs_data_get_string(request, "role"));
	return chat;
}

sarxina_chat_message TestChat::message() const
{
	sarxina_chat_message message{};
	message.platform = "twitch";
	message.id = "test";
	message.user_id = userId.c_str();
	message.user_name = login.c_str();
	message.display_name = displayName.c_str();
	message.role = role;
	message.text = text.c_str();
	message.time_ms = QDateTime::currentMSecsSinceEpoch();
	message.reward_id = rewardId.c_str();
	return message;
}

} // namespace sarxina
