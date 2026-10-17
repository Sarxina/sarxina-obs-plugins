// One chat message, the same whatever platform it came from. TwitchChat makes them (a YouTube chat would make the
// same), and ChatRouter checks them and hands them on to plugins as sarxina_chat_message.
#pragma once

#include "sarxina-obs-plugin-manager.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sarxina {

struct ChatMessage {
	struct Badge {
		std::string name, version;
	};
	struct Emote {
		std::string id;
		size_t begin = 0, end = 0; // bytes of the text; end is one past the last
	};

	std::string platform;
	std::string id;
	std::string userId;
	std::string userName;
	std::string displayName;
	sarxina_role role = SARXINA_ROLE_EVERYONE;
	std::vector<Badge> badges;
	std::string text;
	std::vector<Emote> emotes;
	int64_t timeMs = 0;
	std::string rewardId; // the channel point reward it was sent with, or empty
};

} // namespace sarxina
