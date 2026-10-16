// Where every chat message goes, from any platform. Chat is untrusted, so a message reaches a plugin only after it has
// passed every step, in this order:
//
//   1. The message itself: valid UTF-8 without control characters in every field, the text 1 to 500 characters long
//      and every other field at most 64. Anything else is dropped.
//      Every plugin that is on and asked for all of chat gets the message now.
//   2. It is "!name" or "!name what follows", with a name some plugin registered. Anything else is ordinary chat.
//   3. That plugin is on.
//   4. The chatter has commands to spare: 3 at once, then one more every 3 seconds.
//   5. The chatter's role is high enough for the command. Finer permission rules, when there are any, go here.
//   6. The command is not cooling down.
//   7. The plugin's handler runs.
#pragma once

#include "chat.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace sarxina {

struct Plugin;

class ChatRouter {
public:
	enum class Result {
		Chat,        // not a command
		Ran,         // a plugin's command ran
		NotText,     // dropped: not valid UTF-8, control characters, empty or overlong fields
		TooLong,     // dropped: more than 500 characters
		PluginOff,   // a command of a plugin that is off
		TooMany,     // the chatter sent commands too fast
		NotAllowed,  // the chatter's role is too low
		CoolingDown, // the command ran too recently
	};
	struct Routed {
		Result result = Result::Chat;
		const Plugin *plugin = nullptr; // whose command it was
	};

	// Takes in a plugin's commands, and its wish for all of chat. A command with a bad name, or a name another command
	// has, is left out and described in `problems`.
	void add(Plugin &plugin, std::vector<std::string> &problems);
	// `nowMs` is any clock in milliseconds that only goes forward.
	Routed route(const ChatMessage &message, double nowMs);

private:
	struct Command {
		Plugin *plugin;
		const sarxina_chat_command *command;
		double lastRun;
	};
	struct Allowance {
		double commands; // left to the chatter
		double at;       // when that was counted
	};

	bool spare(const ChatMessage &message, double nowMs);

	std::vector<Plugin *> listeners_; // the plugins that want all of chat
	std::vector<Command> commands_;
	std::unordered_map<std::string, size_t> names_; // names and aliases, into commands_
	std::unordered_map<std::string, Allowance> allowances_;
};

} // namespace sarxina
