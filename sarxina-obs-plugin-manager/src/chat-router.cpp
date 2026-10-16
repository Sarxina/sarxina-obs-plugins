#include "chat-router.hpp"

#include "plugin.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace sarxina {

namespace {

constexpr long kLongestText = 500;  // characters, the same as Twitch allows
constexpr long kLongestField = 64;  // characters, for names and ids
constexpr size_t kLongestName = 25; // of a command
constexpr double kBurst = 3;        // commands a chatter can send at once
constexpr double kRefillMs = 3000;  // after that, one more this often
constexpr size_t kChattersKept = 1000;

// The number of characters in `text` if it is valid UTF-8 without control characters, otherwise -1.
long characters(const std::string &text)
{
	static constexpr uint32_t kLeast[] = {0, 0, 0x80, 0x800, 0x10000}; // by length, to catch overlong encodings
	long count = 0;
	for (size_t i = 0; i < text.size(); count++) {
		const auto lead = static_cast<unsigned char>(text[i]);
		size_t length;
		uint32_t code;
		if (lead < 0x80) {
			length = 1, code = lead;
		} else if ((lead & 0xE0) == 0xC0) {
			length = 2, code = lead & 0x1Fu;
		} else if ((lead & 0xF0) == 0xE0) {
			length = 3, code = lead & 0x0Fu;
		} else if ((lead & 0xF8) == 0xF0) {
			length = 4, code = lead & 0x07u;
		} else {
			return -1;
		}
		if (i + length > text.size())
			return -1;
		for (size_t k = 1; k < length; k++) {
			const auto next = static_cast<unsigned char>(text[i + k]);
			if ((next & 0xC0) != 0x80)
				return -1;
			code = code << 6 | (next & 0x3Fu);
		}
		if (code < kLeast[length] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
			return -1;
		if (code < 0x20 || (code >= 0x7F && code <= 0x9F)) // C0 and C1 control characters, and DEL
			return -1;
		i += length;
	}
	return count;
}

bool field(const std::string &text)
{
	const long length = characters(text);
	return length >= 0 && length <= kLongestField;
}

// Step 1. Chat if the message may go on, otherwise why it is dropped.
ChatRouter::Result check(const ChatMessage &message)
{
	using Result = ChatRouter::Result;
	if (message.userName.empty())
		return Result::NotText;
	for (const std::string *text :
	     {&message.platform, &message.id, &message.userId, &message.userName, &message.displayName})
		if (!field(*text))
			return Result::NotText;
	for (const ChatMessage::Badge &badge : message.badges)
		if (!field(badge.name) || !field(badge.version))
			return Result::NotText;
	for (const ChatMessage::Emote &emote : message.emotes)
		if (!field(emote.id) || emote.begin >= emote.end || emote.end > message.text.size())
			return Result::NotText;

	const long length = characters(message.text);
	if (length <= 0)
		return Result::NotText;
	return length > kLongestText ? Result::TooLong : Result::Chat;
}

bool validName(const char *name)
{
	const size_t length = name ? std::char_traits<char>::length(name) : 0;
	return length >= 1 && length <= kLongestName &&
	       std::all_of(name, name + length,
			   [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::string lower(std::string text)
{
	for (char &c : text)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return text;
}

// The message as plugins see it. It points into `message` and the two lists, which have to outlive it.
sarxina_chat_message view(const ChatMessage &message, std::vector<sarxina_badge> &badges,
			  std::vector<sarxina_emote> &emotes)
{
	for (const ChatMessage::Badge &badge : message.badges)
		badges.push_back({badge.name.c_str(), badge.version.c_str()});
	for (const ChatMessage::Emote &emote : message.emotes)
		emotes.push_back({emote.id.c_str(), emote.begin, emote.end});
	return {
		message.platform.c_str(),
		message.id.c_str(),
		message.userId.c_str(),
		message.userName.c_str(),
		message.displayName.c_str(),
		message.role,
		badges.data(),
		badges.size(),
		message.text.c_str(),
		emotes.data(),
		emotes.size(),
		message.timeMs,
	};
}

std::string trimmed(const std::string &text)
{
	const size_t first = text.find_first_not_of(' ');
	return first == std::string::npos ? std::string() : text.substr(first, text.find_last_not_of(' ') - first + 1);
}

} // namespace

void ChatRouter::add(Plugin &plugin, std::vector<std::string> &problems)
{
	const sarxina_plugin_info &info = *plugin.info;
	if (info.chat)
		listeners_.push_back(&plugin);
	for (size_t i = 0; i < info.command_count && info.commands; i++) {
		const sarxina_chat_command &command = info.commands[i];
		std::vector<const char *> names{command.name};
		for (const char *const *alias = command.aliases; alias && *alias; alias++)
			names.push_back(*alias);

		std::string problem;
		for (size_t n = 0; n < names.size() && problem.empty(); n++) {
			const std::string name = names[n] ? names[n] : "";
			if (!validName(names[n]))
				problem = "\"" + name + "\" is not a command name (1 to 25 of a-z, 0-9 and _)";
			else if (names_.count(name) || std::count(names.begin(), names.begin() + n, name))
				problem = "!" + name + " is taken";
		}
		if (problem.empty() && !command.run)
			problem = "!" + std::string(command.name) + " has nothing to run";
		if (!problem.empty()) {
			problems.push_back(problem);
			continue;
		}

		commands_.push_back({&plugin, &command, -std::numeric_limits<double>::infinity()});
		for (const char *name : names)
			names_[name] = commands_.size() - 1;
	}
}

ChatRouter::Routed ChatRouter::route(const ChatMessage &message, double nowMs)
{
	// 1
	if (const Result problem = check(message); problem != Result::Chat)
		return {problem};
	std::vector<sarxina_badge> badges;
	std::vector<sarxina_emote> emotes;
	const sarxina_chat_message seen = view(message, badges, emotes);
	for (const Plugin *listener : listeners_)
		if (listener->enabled)
			listener->info->chat(listener->info->data, &seen);

	// 2
	const std::string &text = message.text;
	if (text[0] != '!')
		return {};
	const size_t end = std::min(text.find(' '), text.size());
	const auto found = names_.find(lower(text.substr(1, end - 1)));
	if (found == names_.end())
		return {};
	Command &command = commands_[found->second];
	const Plugin *plugin = command.plugin;

	// 3 to 6
	if (!plugin->enabled)
		return {Result::PluginOff, plugin};
	if (!spare(message, nowMs))
		return {Result::TooMany, plugin};
	if (message.role < command.command->who)
		return {Result::NotAllowed, plugin};
	if (nowMs - command.lastRun < command.command->cooldown_ms)
		return {Result::CoolingDown, plugin};
	command.lastRun = nowMs;

	// 7
	const std::string args = trimmed(text.substr(end));
	command.command->run(plugin->info->data, args.c_str(), &seen);
	return {Result::Ran, plugin};
}

// Step 4: every chatter has a few commands to spare, which come back slowly as time passes.
bool ChatRouter::spare(const ChatMessage &message, double nowMs)
{
	const auto refilled = [nowMs](const Allowance &allowance) {
		return std::min(kBurst, allowance.commands + (nowMs - allowance.at) / kRefillMs);
	};
	if (allowances_.size() >= kChattersKept) // forget chatters who have all their commands back
		for (auto it = allowances_.begin(); it != allowances_.end();)
			it = refilled(it->second) >= kBurst ? allowances_.erase(it) : std::next(it);

	const std::string chatter = message.platform + "/" + (message.userId.empty() ? message.userName : message.userId);
	Allowance &allowance = allowances_.try_emplace(chatter, Allowance{kBurst, nowMs}).first->second;
	allowance.commands = refilled(allowance);
	allowance.at = nowMs;
	if (allowance.commands < 1)
		return false;
	allowance.commands -= 1;
	return true;
}

} // namespace sarxina
