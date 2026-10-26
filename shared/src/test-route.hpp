// Ways into a plugin without Twitch, for a demo or a test: requests of the plugin's own in obs-websocket
// (CallVendorRequest), and chat messages made up from what such a request says, handed over the way the Sarxina OBS
// Plugin Manager hands over chat.
//
// obs-websocket's vendor API is a few calls on its own procedure handler (its lib/obs-websocket-api.h), written out here
// so that nothing of obs-websocket is needed to build. Without obs-websocket there are simply no such requests.
#pragma once

#include "sarxina-obs-plugin-manager.h"

#include <obs.h>

#include <string>

namespace sarxina {

// What one request does: reads `request` and fills in `response`. Called on one of obs-websocket's threads.
using VendorRequest = void (*)(obs_data_t *request, obs_data_t *response, void *data);

// Makes `request` answer the request `type` of the vendor `vendor`. False when obs-websocket is not there or refused it.
bool addVendorRequest(const char *vendor, const char *type, VendorRequest request, void *data);

// A chat message made up for a demo or a test. It holds its own text; message() points into it, so it lives only as
// long as this does.
struct TestChat {
	std::string login, displayName, userId, text, rewardId;
	sarxina_role role = SARXINA_ROLE_EVERYONE;

	// From a request's "user", "text", "reward_id" and "role" ("moderator", "broadcaster", ...; everyone if left
	// out), made safe the way the manager makes chat safe.
	static TestChat fromRequest(obs_data_t *request);
	sarxina_chat_message message() const;
};

} // namespace sarxina
