#include "twitch-chat.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QRandomGenerator>

#include <algorithm>
#include <map>
#include <string_view>

namespace sarxina {

namespace {

constexpr qint64 kQuietMs = 60000; // after this long without a word from Twitch, ask whether it is still there
constexpr qint64 kGoneMs = 75000;  // and after this long, connect again
constexpr size_t npos = std::string::npos;

bool digits(std::string_view text)
{
	return std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// One line of Twitch's chat protocol (IRC with IRCv3 tags): "@tags :prefix COMMAND params".
struct Line {
	std::map<std::string, std::string, std::less<>> tags;
	std::string prefix, command, params;

	std::string tag(std::string_view key) const
	{
		const auto found = tags.find(key);
		return found == tags.end() ? std::string() : found->second;
	}
};

// Tag values have ; space \ CR LF written as \: \s \\ \r \n.
std::string unescape(std::string_view value)
{
	std::string out;
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] != '\\') {
			out += value[i];
			continue;
		}
		if (++i == value.size())
			break;
		switch (value[i]) {
		case ':': out += ';'; break;
		case 's': out += ' '; break;
		case 'r': out += '\r'; break;
		case 'n': out += '\n'; break;
		default: out += value[i]; break;
		}
	}
	return out;
}

Line split(const std::string &text)
{
	Line line;
	size_t at = 0;
	if (!text.empty() && text[0] == '@') {
		const size_t end = text.find(' ');
		if (end == npos)
			return {};
		for (std::string_view tags(text.data() + 1, end - 1); !tags.empty();) {
			const std::string_view tag = tags.substr(0, tags.find(';'));
			const size_t equals = tag.find('=');
			line.tags[std::string(tag.substr(0, equals))] = equals == npos ? "" : unescape(tag.substr(equals + 1));
			tags.remove_prefix(std::min(tags.size(), tag.size() + 1));
		}
		at = end + 1;
	}
	if (at < text.size() && text[at] == ':') {
		const size_t end = text.find(' ', at);
		if (end == npos)
			return {};
		line.prefix = text.substr(at + 1, end - at - 1);
		at = end + 1;
	}
	const size_t end = text.find(' ', at);
	line.command = text.substr(std::min(at, text.size()), end == npos ? npos : end - at);
	if (end != npos)
		line.params = text.substr(end + 1);
	return line;
}

std::string nick(const std::string &prefix)
{
	return prefix.substr(0, prefix.find('!'));
}

bool number(std::string_view text, size_t &value)
{
	if (text.empty() || text.size() > 9 || !digits(text))
		return false;
	value = std::stoul(std::string(text));
	return true;
}

// Where the character with this index starts, in bytes; npos past the end.
size_t byteOffset(const std::string &text, size_t index)
{
	size_t at = 0;
	for (size_t seen = 0; seen < index; seen++) {
		if (at >= text.size())
			return npos;
		at++;
		while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80)
			at++;
	}
	return at <= text.size() ? at : npos;
}

// "25:0-4,12-16/1902:6-10": emote 25 at characters 0 to 4 and 12 to 16, emote 1902 at 6 to 10, last one included.
std::vector<ChatMessage::Emote> emotes(const std::string &list, const std::string &text)
{
	std::vector<ChatMessage::Emote> found;
	for (std::string_view rest = list; !rest.empty();) {
		const std::string_view emote = rest.substr(0, rest.find('/'));
		rest.remove_prefix(std::min(rest.size(), emote.size() + 1));
		const size_t colon = emote.find(':');
		if (colon == npos)
			continue;
		for (std::string_view ranges = emote.substr(colon + 1); !ranges.empty();) {
			const std::string_view range = ranges.substr(0, ranges.find(','));
			ranges.remove_prefix(std::min(ranges.size(), range.size() + 1));
			const size_t dash = range.find('-');
			size_t first = 0, last = 0;
			if (dash == npos || !number(range.substr(0, dash), first) || !number(range.substr(dash + 1), last) ||
			    last < first)
				continue;
			const size_t begin = byteOffset(text, first), end = byteOffset(text, last + 1);
			if (begin != npos && end != npos && begin < end)
				found.push_back({std::string(emote.substr(0, colon)), begin, end});
		}
	}
	std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.begin < b.begin; });
	return found;
}

// "broadcaster/1,subscriber/12"
std::vector<ChatMessage::Badge> badges(const std::string &list)
{
	std::vector<ChatMessage::Badge> found;
	for (std::string_view rest = list; !rest.empty();) {
		const std::string_view badge = rest.substr(0, rest.find(','));
		rest.remove_prefix(std::min(rest.size(), badge.size() + 1));
		const size_t slash = badge.find('/');
		found.push_back({std::string(badge.substr(0, slash)),
				 slash == npos ? std::string() : std::string(badge.substr(slash + 1))});
	}
	return found;
}

sarxina_role role(const Line &line, const std::vector<ChatMessage::Badge> &badges)
{
	const auto has = [&badges](const char *name) {
		return std::any_of(badges.begin(), badges.end(), [name](const auto &badge) { return badge.name == name; });
	};
	if (has("broadcaster"))
		return SARXINA_ROLE_BROADCASTER;
	if (line.tag("mod") == "1" || has("moderator"))
		return SARXINA_ROLE_MODERATOR;
	if (has("vip") || line.tags.count("vip"))
		return SARXINA_ROLE_VIP;
	if (line.tag("subscriber") == "1" || has("subscriber") || has("founder"))
		return SARXINA_ROLE_SUBSCRIBER;
	return SARXINA_ROLE_EVERYONE;
}

const char *name(TwitchChat::State state)
{
	switch (state) {
	case TwitchChat::State::Off: return "off";
	case TwitchChat::State::Connecting: return "connecting";
	case TwitchChat::State::Reading: return "reading";
	case TwitchChat::State::Retrying: return "cannot reach Twitch, trying again";
	}
	return "";
}

} // namespace

TwitchChat::TwitchChat(QString server, QObject *parent) : QObject(parent), server_(std::move(server))
{
	connect(&socket_, &WebSocket::opened, this, &TwitchChat::opened);
	connect(&socket_, &WebSocket::received, this, &TwitchChat::received);
	connect(&socket_, &WebSocket::closed, this, [this] {
		keepAlive_.stop();
		setState(State::Retrying);
	});
	keepAlive_.setInterval(5000);
	connect(&keepAlive_, &QTimer::timeout, this, &TwitchChat::keepAlive);
}

bool TwitchChat::validChannel(const QString &channel)
{
	return !channel.isEmpty() && channel.size() <= 25 && std::all_of(channel.begin(), channel.end(), [](QChar c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	});
}

void TwitchChat::join(const QString &channel)
{
	if (!validChannel(channel))
		return;
	channel_ = channel.toLower();
	nick_ = "justinfan" + std::to_string(QRandomGenerator::global()->bounded(10000, 100000));
	setState(State::Connecting);
	socket_.open(server_);
}

void TwitchChat::leave()
{
	socket_.close();
	keepAlive_.stop();
	setState(State::Off);
}

void TwitchChat::opened()
{
	heard_.start();
	pinged_ = false;
	keepAlive_.start();
	socket_.send("CAP REQ :twitch.tv/tags twitch.tv/commands");
	socket_.send(QByteArray("NICK ") + nick_.c_str());
	socket_.send("JOIN #" + channel_.toUtf8());
}

void TwitchChat::received(const QByteArray &text)
{
	heard_.start();
	pinged_ = false;
	for (const QByteArray &part : text.split('\n')) {
		const QByteArray trimmed = part.endsWith('\r') ? part.chopped(1) : part;
		if (!trimmed.isEmpty())
			line(trimmed.toStdString());
	}
}

void TwitchChat::line(const std::string &text)
{
	if (text.rfind("PING", 0) == 0) {
		socket_.send(QByteArray::fromStdString("PONG" + text.substr(4)));
		return;
	}
	ChatMessage chat;
	if (parse(text, chat)) {
		emit message(chat);
		return;
	}
	const Line parsed = split(text);
	if (parsed.command == "RECONNECT") { // Twitch is about to restart this server
		setState(State::Connecting);
		socket_.open(server_);
	} else if (parsed.command == "JOIN" && nick(parsed.prefix) == nick_) {
		setState(State::Reading);
	}
}

void TwitchChat::keepAlive()
{
	if (heard_.elapsed() > kGoneMs) {
		setState(State::Connecting);
		socket_.open(server_);
	} else if (heard_.elapsed() > kQuietMs && !pinged_) {
		socket_.send("PING :tmi.twitch.tv");
		pinged_ = true;
	}
}

void TwitchChat::setState(State state)
{
	if (state == state_)
		return;
	state_ = state;
	blog(LOG_INFO, "[Sarxina OBS Plugin Manager] Twitch chat #%s: %s", channel_.toUtf8().constData(), name(state));
	emit stateChanged();
}

bool TwitchChat::parse(const std::string &text, ChatMessage &message)
{
	const Line line = split(text);
	const size_t colon = line.params.find(" :");
	if (line.command != "PRIVMSG" || colon == npos)
		return false;

	std::string body = line.params.substr(colon + 2);
	// "/me" messages arrive wrapped as \1ACTION ...\1; the emote positions count from inside it.
	static const std::string kAction = "\x01" "ACTION ";
	if (body.size() > kAction.size() && body.compare(0, kAction.size(), kAction) == 0 && body.back() == '\x01')
		body = body.substr(kAction.size(), body.size() - kAction.size() - 1);

	message = ChatMessage{};
	message.platform = "twitch";
	message.id = line.tag("id");
	message.userId = line.tag("user-id");
	message.userName = nick(line.prefix);
	message.displayName = line.tag("display-name");
	if (message.displayName.empty())
		message.displayName = message.userName;
	message.badges = badges(line.tag("badges"));
	message.role = role(line, message.badges);
	message.emotes = emotes(line.tag("emotes"), body);
	message.text = std::move(body);
	const std::string time = line.tag("tmi-sent-ts");
	message.timeMs = !time.empty() && time.size() <= 15 && digits(time) ? std::stoll(time)
									      : QDateTime::currentMSecsSinceEpoch();
	return true;
}

} // namespace sarxina
