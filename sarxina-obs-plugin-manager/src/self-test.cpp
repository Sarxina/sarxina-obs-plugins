// The Sarxina OBS Plugin Manager's self-test (see shared/src/self-test.hpp). It needs no OBS and no internet: Twitch
// and VTube Studio are played by small stand-in websocket servers on this computer.
//
//   1. Lines from Twitch become the right chat messages, and the Twitch account OBS is connected to is read from its
//      profile (only the name).
//   2. The checks drop what they should and let the rest through.
//   3. Commands reach the plugin that registered them, and only when they may.
//   4. The whole way: a stand-in Twitch sends chat over a real websocket, and a plugin's handler gets the command.
//   5. VTube Studio over a real websocket: allowing the manager once, logging in with the remembered token, never
//      asking again on its own, and a plugin's request and its answer.
// Given a folder, it also paints the dock into dock.png there, and what a plugin's gear shows into settings.png.
//
// Inside OBS (SARXINA_OBS_PLUGIN_MANAGER_SELFTEST=1) checkInObs also lists the plugins that registered through OBS
// and, once VTube Studio is connected, sends it one request that only reads, the way a plugin would.
#include "self-test.hpp"

#include "manager-dock.hpp"
#include "manager.hpp"
#include "properties-form.hpp"

#include <QCoreApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPixmap>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QVBoxLayout>

#include <deque>
#include <iterator>
#include <string>
#include <vector>

using namespace sarxina;
using Result = ChatRouter::Result;

namespace {

// ---- helpers --------------------------------------------------------------------------------------------------------

// Runs Qt's event loop until `done` or until `ms` have passed.
template<typename Done> bool waitFor(Done done, int ms = 5000)
{
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < ms) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(2);
	}
	return done();
}

// Counts checks and keeps the first few that failed, for one line of results.
struct Tally {
	int passed = 0, total = 0;
	std::string failed;

	void expect(bool ok, const std::string &what)
	{
		total++;
		passed += ok;
		if (!ok && failed.size() < 300)
			failed += (failed.empty() ? "; failed: " : ", ") + what;
	}
	bool report(const char *group, const char *what)
	{
		const bool ok = passed == total;
		test::report(ok, "%s: %d of %d %s%s: %s", group, passed, total, what, failed.c_str(), ok ? "OK" : "FAILED");
		return ok;
	}
};

// A websocket server on this computer standing in for Twitch or VTube Studio. It talks to one connection at a time
// and keeps the text messages it gets.
class StandIn : public QObject {
public:
	StandIn()
	{
		server_.listen(QHostAddress::LocalHost, 0);
		connect(&server_, &QTcpServer::newConnection, this, [this] {
			while (QTcpSocket *socket = server_.nextPendingConnection()) {
				client_ = socket;
				buffer_.clear();
				upgraded_ = false;
				connections++;
				connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
					if (socket == client_)
						read();
				});
			}
		});
	}

	int port() const { return server_.serverPort(); }
	QString url() const { return QStringLiteral("ws://127.0.0.1:%1").arg(port()); }

	// Waits for the next text message.
	bool next(QByteArray &text, int ms = 5000)
	{
		if (!waitFor([this] { return !got.empty(); }, ms))
			return false;
		text = got.front();
		got.pop_front();
		return true;
	}
	bool nextJson(QJsonObject &message)
	{
		QByteArray text;
		if (!next(text))
			return false;
		message = QJsonDocument::fromJson(text).object();
		return true;
	}

	void send(const QByteArray &text)
	{
		QByteArray frame(1, static_cast<char>(0x81)); // FIN and text: a whole text message
		if (text.size() < 126) {
			frame += char(text.size());
		} else {
			frame += char(126);
			frame += char((text.size() >> 8) & 0xFF);
			frame += char(text.size() & 0xFF);
		}
		frame += text;
		if (client_) {
			client_->write(frame);
			client_->flush();
		}
	}
	// VTube Studio's answer to a request.
	void answer(const QJsonObject &request, const char *type, const QJsonObject &data)
	{
		send(QJsonDocument(QJsonObject{{"apiName", "VTubeStudioPublicAPI"},
					       {"apiVersion", "1.0"},
					       {"requestID", request.value("requestID")},
					       {"messageType", type},
					       {"data", data}})
			     .toJson(QJsonDocument::Compact));
	}
	void drop()
	{
		if (client_)
			client_->abort();
	}

	std::deque<QByteArray> got;
	int connections = 0;

private:
	void read()
	{
		buffer_ += client_->readAll();
		if (!upgraded_) {
			const qsizetype end = buffer_.indexOf("\r\n\r\n");
			if (end < 0)
				return;
			QByteArray key;
			for (const QByteArray &line : buffer_.left(end).split('\n'))
				if (line.toLower().startsWith("sec-websocket-key:"))
					key = line.mid(18).trimmed();
			buffer_.remove(0, end + 4);
			const QByteArray accept =
				QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1)
					.toBase64();
			client_->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
				       "Sec-WebSocket-Accept: " +
				       accept + "\r\n\r\n");
			upgraded_ = true;
		}
		// Frames from a client are masked.
		for (;;) {
			if (buffer_.size() < 2)
				return;
			const auto first = static_cast<unsigned char>(buffer_[0]);
			const auto second = static_cast<unsigned char>(buffer_[1]);
			qsizetype length = second & 0x7F, at = 2;
			if (length == 126) {
				if (buffer_.size() < 4)
					return;
				length = static_cast<unsigned char>(buffer_[2]) << 8 | static_cast<unsigned char>(buffer_[3]);
				at = 4;
			} else if (length == 127) {
				if (buffer_.size() < 10)
					return;
				length = 0;
				for (int i = 0; i < 8; i++)
					length = length << 8 | static_cast<unsigned char>(buffer_[2 + i]);
				at = 10;
			}
			const bool masked = second & 0x80;
			if (buffer_.size() < at + (masked ? 4 : 0) + length)
				return;
			const QByteArray mask = masked ? buffer_.mid(at, 4) : QByteArray();
			at += masked ? 4 : 0;
			QByteArray payload = buffer_.mid(at, length);
			for (qsizetype i = 0; masked && i < payload.size(); i++)
				payload[i] = char(payload[i] ^ mask[i % 4]);
			buffer_.remove(0, at + length);
			if ((first & 0x0F) == 0x1)
				got.push_back(payload);
			else if ((first & 0x0F) == 0x8)
				client_->disconnectFromHost();
		}
	}

	QTcpServer server_;
	QPointer<QTcpSocket> client_;
	QByteArray buffer_;
	bool upgraded_ = false;
};

// ---- a plugin that only exists here ---------------------------------------------------------------------------------

struct Heard {
	int runs = 0;
	std::string command, args, user, display, text, firstEmote;
	sarxina_role role = SARXINA_ROLE_EVERYONE;
	size_t badges = 0;
	int chats = 0; // messages from the feed of all of chat
	std::string lastChat, lastReward;
	bool enabled = false;
	std::string mode = "real";
};

void heardChat(void *data, const sarxina_chat_message *message)
{
	Heard &heard = *static_cast<Heard *>(data);
	heard.chats++;
	heard.lastChat = message->text;
	heard.lastReward = message->reward_id;
}

void record(void *data, const char *command, const char *args, const sarxina_chat_message *message)
{
	Heard &heard = *static_cast<Heard *>(data);
	heard.runs++;
	heard.command = command;
	heard.args = args;
	heard.user = message->user_name;
	heard.display = message->display_name;
	heard.text = message->text;
	heard.role = message->role;
	heard.badges = message->badge_count;
	heard.firstEmote = message->emote_count ? std::string(message->text + message->emotes[0].begin,
								     message->emotes[0].end - message->emotes[0].begin)
						 : std::string();
}

const char *const kHelloAliases[] = {"hi", nullptr};
const sarxina_chat_command kCommands[] = {
	{"hello", kHelloAliases, SARXINA_ROLE_EVERYONE, 0,
	 [](void *data, const char *args, const sarxina_chat_message *message) { record(data, "hello", args, message); }},
	{"mods", nullptr, SARXINA_ROLE_MODERATOR, 0,
	 [](void *data, const char *args, const sarxina_chat_message *message) { record(data, "mods", args, message); }},
	{"slow", nullptr, SARXINA_ROLE_EVERYONE, 5000,
	 [](void *data, const char *args, const sarxina_chat_message *message) { record(data, "slow", args, message); }},
};

// A second plugin whose first two commands cannot be taken: the name is in use, and the other is not a name.
const sarxina_chat_command kClashing[] = {
	{"hello", nullptr, SARXINA_ROLE_EVERYONE, 0, kCommands[0].run},
	{"Bad-Name", nullptr, SARXINA_ROLE_EVERYONE, 0, kCommands[0].run},
	{"other", nullptr, SARXINA_ROLE_EVERYONE, 0,
	 [](void *data, const char *args, const sarxina_chat_message *message) { record(data, "other", args, message); }},
};

sarxina_plugin_info testPlugin(Heard &heard, const char *id = "selftest", const char *name = "Test plugin")
{
	sarxina_plugin_info info{};
	info.api = SARXINA_OBS_PLUGIN_MANAGER_API;
	info.id = id;
	info.name = name;
	info.description = "Only here for the self-test: a check box, a number and a list.";
	info.data = &heard;
	info.set_enabled = [](void *data, bool enabled) { static_cast<Heard *>(data)->enabled = enabled; };
	info.get_properties = [](void *) {
		obs_properties_t *properties = obs_properties_create();
		obs_properties_add_bool(properties, "loud", "Loud");
		obs_properties_add_int(properties, "count", "How many", 1, 10, 1);
		obs_property_t *mode = obs_properties_add_list(properties, "mode", "Mode", OBS_COMBO_TYPE_LIST,
							       OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(mode, "Real: starts and stops OBS", "real");
		obs_property_list_add_string(mode, "Practice: only this panel changes", "practice");
		return properties;
	};
	info.get_settings = [](void *data, obs_data_t *settings) {
		obs_data_set_string(settings, "mode", static_cast<Heard *>(data)->mode.c_str());
		obs_data_set_int(settings, "count", 3);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<Heard *>(data)->mode = obs_data_get_string(settings, "mode");
	};
	info.commands = kCommands;
	info.command_count = std::size(kCommands);
	info.chat = heardChat;
	return info;
}

ChatMessage chatter(const std::string &name, sarxina_role role, const std::string &text)
{
	ChatMessage message;
	message.platform = "twitch";
	message.id = "id-" + name;
	message.userId = "user-" + name;
	message.userName = name;
	message.displayName = name;
	message.role = role;
	message.text = text;
	return message;
}

// ---- 1. Twitch lines ------------------------------------------------------------------------------------------------

bool parsing()
{
	Tally tally;
	ChatMessage m;

	tally.expect(TwitchChat::parse("@badge-info=subscriber/12;badges=broadcaster/1,subscriber/12;color=#FF0000;"
				       "display-name=Sarxina;emotes=25:0-4,12-16/1902:6-10;id=b34ccfc7;mod=0;"
				       "subscriber=1;tmi-sent-ts=1700000000123;user-id=4242 "
				       ":sarxina!sarxina@sarxina.tmi.twitch.tv PRIVMSG #sarxina :Kappa Keepo Kappa",
				       m),
		     "a full message");
	tally.expect(m.platform == "twitch" && m.id == "b34ccfc7" && m.userId == "4242" && m.userName == "sarxina" &&
			     m.displayName == "Sarxina" && m.text == "Kappa Keepo Kappa" && m.timeMs == 1700000000123 &&
			     m.rewardId.empty(),
		     "its fields");
	tally.expect(m.role == SARXINA_ROLE_BROADCASTER && m.badges.size() == 2 && m.badges[1].name == "subscriber" &&
			     m.badges[1].version == "12",
		     "its badges and role");
	tally.expect(m.emotes.size() == 3 && m.emotes[0].id == "25" && m.emotes[0].begin == 0 && m.emotes[0].end == 5 &&
			     m.emotes[1].id == "1902" && m.emotes[1].begin == 6 && m.emotes[2].begin == 12 &&
			     m.emotes[2].end == 17,
		     "its emotes, in order");

	// "/me", with a two-byte character before the emote: Twitch counts characters, the message gives bytes.
	tally.expect(TwitchChat::parse("@display-name=;emotes=25:6-10;mod=1;user-id=1 :someone!someone@x PRIVMSG #c "
				       ":\x01" "ACTION h\xC3\xA9llo Kappa\x01",
				       m) &&
			     m.text == "h\xC3\xA9llo Kappa" && m.displayName == "someone" && m.role == SARXINA_ROLE_MODERATOR &&
			     m.emotes.size() == 1 && m.emotes[0].begin == 7 && m.emotes[0].end == 12,
		     "a /me message");
	tally.expect(TwitchChat::parse("@display-name=Some\\sone\\:x;vip=1;user-id=2 :v!v@v PRIVMSG #c :hi", m) &&
			     m.displayName == "Some one;x" && m.role == SARXINA_ROLE_VIP,
		     "escaped tags");
	tally.expect(TwitchChat::parse("@badges=founder/0;user-id=3 :f!f@f PRIVMSG #c :hey", m) &&
			     m.role == SARXINA_ROLE_SUBSCRIBER,
		     "a founder");
	tally.expect(TwitchChat::parse("@emotes=25:40-44,x-1;user-id=4 :e!e@e PRIVMSG #c :short", m) && m.emotes.empty(),
		     "emotes outside the text");
	tally.expect(TwitchChat::parse(":p!p@p PRIVMSG #c :no tags", m) && m.role == SARXINA_ROLE_EVERYONE &&
			     m.userName == "p" && m.text == "no tags",
		     "a message without tags");
	// A channel point reward that asks for text arrives as the text, with the reward's id.
	tally.expect(TwitchChat::parse("@custom-reward-id=0c7c2a4e-6f1d-4a5b-9a43-2f8a1d6e5b77;display-name=Viewer;"
				       "user-id=9 :viewer!viewer@viewer.tmi.twitch.tv PRIVMSG #sarxina :drop it",
				       m) &&
			     m.rewardId == "0c7c2a4e-6f1d-4a5b-9a43-2f8a1d6e5b77" && m.text == "drop it",
		     "a channel point reward with text");
	for (const char *other : {"PING :tmi.twitch.tv", ":tmi.twitch.tv 001 justinfan1 :Welcome, GLHF!",
				  ":justinfan1!justinfan1@justinfan1.tmi.twitch.tv JOIN #c", ":tmi.twitch.tv RECONNECT",
				  "@id=1 :p!p@p PRIVMSG #c", "", "@", ":"})
		tally.expect(!TwitchChat::parse(other, m), std::string("not a message: ") + other);

	return tally.report("Twitch", "lines read as expected");
}

// ---- 1b. the Twitch account OBS is connected to ---------------------------------------------------------------------

// What a profile's basic.ini looks like with a connected Twitch account. The tokens beside the name are never read.
const char *const kConnectedProfile = "[General]\nName=Streaming\n"
				      "[Auth]\nType=Twitch\n"
				      "[Twitch]\nName=sarxina\nUUID=x\nToken=not-for-us\nRefreshToken=not-for-us\n";

QString accountIn(const char *profile)
{
	config_t *config = nullptr;
	if (config_open_string(&config, profile) != CONFIG_SUCCESS)
		return QStringLiteral("(unreadable)");
	const QString account = twitchAccount(config);
	config_close(config);
	return account;
}

bool account()
{
	Tally tally;
	tally.expect(accountIn(kConnectedProfile) == "sarxina", "the name of the connected account");
	tally.expect(accountIn("[Auth]\nType=YouTube - RTMPS\n[Twitch]\nName=sarxina\n").isEmpty(),
		     "nothing when OBS is connected to another service");
	tally.expect(accountIn("[Twitch]\nName=sarxina\n").isEmpty(), "nothing when the account was disconnected");
	tally.expect(accountIn("[Auth]\nType=Twitch\n[Twitch]\nName=two words\n").isEmpty(), "nothing for a bad name");
	tally.expect(accountIn("[General]\nName=Untitled\n").isEmpty() && twitchAccount(nullptr).isEmpty(),
		     "nothing in a profile without one");

	// Given a copy of a real profile, says which account it has: only the name.
	const QByteArray copy = qgetenv("SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PROFILE");
	config_t *config = nullptr;
	if (!copy.isEmpty() && config_open(&config, copy.constData(), CONFIG_OPEN_EXISTING) == CONFIG_SUCCESS) {
		const QString name = twitchAccount(config);
		config_close(config);
		test::report(true, "Twitch account in %s: %s", copy.constData(),
			     name.isEmpty() ? "none" : name.toUtf8().constData());
	}
	return tally.report("Twitch account", "profiles read as expected");
}

// ---- 2. the checks --------------------------------------------------------------------------------------------------

bool checks()
{
	Tally tally;
	ChatRouter router;
	std::string e500, a501(501, 'a');
	for (int i = 0; i < 500; i++)
		e500 += "\xC3\xA9";
	const struct {
		const char *what;
		std::string text;
		Result expected;
	} cases[] = {
		{"plain text", "hello there", Result::Chat},
		{"an emoji", "\xF0\x9F\x98\x80", Result::Chat},
		{"500 two-byte characters", e500, Result::Chat},
		{"501 characters", a501, Result::TooLong},
		{"an empty text", "", Result::NotText},
		{"a bell", "ding\x07", Result::NotText},
		{"a line break", "one\ntwo", Result::NotText},
		{"DEL", "\x7F", Result::NotText},
		{"a C1 control", "\xC2\x85", Result::NotText},
		{"broken UTF-8", "\xFF\xFE", Result::NotText},
		{"an overlong /", "\xC0\xAF", Result::NotText},
		{"a lone surrogate", "\xED\xA0\x80", Result::NotText},
		{"a cut-off character", "\xE2\x82", Result::NotText},
	};
	for (const auto &c : cases)
		tally.expect(router.route(chatter("viewer", SARXINA_ROLE_EVERYONE, c.text), 0).result == c.expected,
			     c.what);

	ChatMessage badName = chatter("viewer", SARXINA_ROLE_EVERYONE, "hi");
	badName.displayName = "two\nlines";
	tally.expect(router.route(badName, 0).result == Result::NotText, "a name with a line break");
	ChatMessage longName = chatter(std::string(65, 'x'), SARXINA_ROLE_EVERYONE, "hi");
	tally.expect(router.route(longName, 0).result == Result::NotText, "a 65-character name");
	ChatMessage badEmote = chatter("viewer", SARXINA_ROLE_EVERYONE, "hi");
	badEmote.emotes.push_back({"25", 1, 9});
	tally.expect(router.route(badEmote, 0).result == Result::NotText, "an emote past the end");
	ChatMessage badReward = chatter("viewer", SARXINA_ROLE_EVERYONE, "hi");
	badReward.rewardId = "one\ntwo";
	tally.expect(router.route(badReward, 0).result == Result::NotText, "a reward id with a line break");

	return tally.report("checks", "messages handled as expected");
}

// ---- 3. routing -----------------------------------------------------------------------------------------------------

bool routing()
{
	Tally tally;
	Heard heard, other;
	const sarxina_plugin_info info = testPlugin(heard);
	sarxina_plugin_info clashingInfo = testPlugin(other, "clashing", "Clashing plugin");
	clashingInfo.commands = kClashing;
	clashingInfo.command_count = std::size(kClashing);
	Plugin plugin{&info}, clashing{&clashingInfo};

	ChatRouter router;
	std::vector<std::string> problems;
	router.add(plugin, problems);
	tally.expect(problems.empty(), "the test plugin's commands taken");
	router.add(clashing, problems);
	tally.expect(problems.size() == 2, "a taken name and a bad name refused");

	int runs = 0;
	const auto step = [&](double ms, const ChatMessage &message, Result expected, const std::string &what) {
		const ChatRouter::Routed routed = router.route(message, ms);
		const bool ran = heard.runs + other.runs > runs;
		runs = heard.runs + other.runs;
		tally.expect(routed.result == expected && ran == (expected == Result::Ran), what);
	};
	const sarxina_role everyone = SARXINA_ROLE_EVERYONE;

	step(0, chatter("viewer", everyone, "!hello   big  wave  "), Result::Ran, "a command");
	tally.expect(heard.command == "hello" && heard.args == "big  wave" && heard.user == "viewer", "its arguments");
	step(10, chatter("viewer2", everyone, "!HI"), Result::Ran, "an alias, in capitals");
	tally.expect(heard.args.empty(), "no arguments");
	step(20, chatter("viewer3", everyone, "hello"), Result::Chat, "no !");
	step(30, chatter("viewer3", everyone, "!nope"), Result::Chat, "an unknown command");
	step(40, chatter("viewer3", everyone, "!mods"), Result::NotAllowed, "a viewer at a moderators' command");
	step(50, chatter("mod", SARXINA_ROLE_MODERATOR, "!mods"), Result::Ran, "a moderator");
	step(60, chatter("streamer", SARXINA_ROLE_BROADCASTER, "!mods"), Result::Ran, "the broadcaster");
	step(70, chatter("sub", SARXINA_ROLE_SUBSCRIBER, "!slow"), Result::Ran, "a command with a cooldown");
	step(1070, chatter("viewer4", everyone, "!slow"), Result::CoolingDown, "the same, a second later");
	step(5071, chatter("viewer4", everyone, "!slow"), Result::Ran, "the same, once cooled down");
	for (int i = 0; i < 3; i++)
		step(6000, chatter("spammer", everyone, "!hello"), Result::Ran, "three at once");
	step(6000, chatter("spammer", everyone, "!hello"), Result::TooMany, "a fourth at once");
	step(9000, chatter("spammer", everyone, "!hello"), Result::Ran, "one more, 3 seconds on");
	step(9000, chatter("spammer", everyone, "!hello"), Result::TooMany, "and not two");
	step(9500, chatter("viewer5", everyone, "!other"), Result::Ran, "the other plugin's command");
	tally.expect(other.command == "other", "run by the other plugin");

	ChatMessage withEmote = chatter("viewer6", SARXINA_ROLE_VIP, "!hello Kappa");
	withEmote.badges = {{"vip", "1"}};
	withEmote.emotes = {{"25", 7, 12}};
	withEmote.displayName = "Viewer6";
	step(9600, withEmote, Result::Ran, "a command with an emote");
	tally.expect(heard.firstEmote == "Kappa" && heard.badges == 1 && heard.role == SARXINA_ROLE_VIP &&
			     heard.display == "Viewer6" && heard.text == "!hello Kappa",
		     "the message as the plugin sees it");

	tally.expect(heard.chats == 18 && heard.lastChat == "!hello Kappa" && heard.lastReward.empty(),
		     "all of chat for the plugin that wants it");

	plugin.enabled = false;
	step(20000, chatter("viewer", everyone, "!hello"), Result::PluginOff, "a plugin that is off");
	tally.expect(heard.chats == 18 && other.chats == 19, "and none for a plugin that is off");
	router.route(chatter("viewer", everyone, "bad\x01"), 20001);
	tally.expect(other.chats == 19, "nor anything the checks dropped");

	return tally.report("routing", "steps as expected");
}

// ---- 4. the whole way, over a websocket -----------------------------------------------------------------------------

bool twitchConnection()
{
	Tally tally;
	StandIn twitch;
	Heard heard;
	const sarxina_plugin_info info = testPlugin(heard);
	Manager manager(Manager::Use::SelfTest, twitch.url());
	tally.expect(manager.add(&info) && !heard.enabled, "a new plugin registered, not added yet");
	manager.setEnabled(*manager.plugins()[0], true);
	tally.expect(heard.enabled, "added");

	const auto handshake = [&](const char *when) {
		QByteArray cap, nick, join;
		const bool ok = twitch.next(cap) && twitch.next(nick) && twitch.next(join);
		tally.expect(ok && cap == "CAP REQ :twitch.tv/tags twitch.tv/commands" && nick.startsWith("NICK justinfan") &&
				     join == "JOIN #sarxina",
			     std::string("logging in anonymously") + when);
		return nick.mid(5);
	};

	manager.followTwitchAccount("Sarxina");
	const QByteArray nick = handshake("");
	twitch.send(":" + nick + "!" + nick + "@" + nick + ".tmi.twitch.tv JOIN #sarxina");
	tally.expect(waitFor([&] { return manager.twitch().state() == TwitchChat::State::Reading; }),
		     "reading once Twitch says it joined");

	QByteArray pong;
	twitch.send("PING :tmi.twitch.tv");
	tally.expect(twitch.next(pong) && pong == "PONG :tmi.twitch.tv", "answering Twitch's ping");

	// Two lines in one websocket message, the way Twitch sends them.
	twitch.send("@badges=moderator/1;display-name=Mod;id=a1;mod=1;user-id=7;tmi-sent-ts=1700000000000 "
		    ":mod!mod@mod.tmi.twitch.tv PRIVMSG #sarxina :!hello from the socket\r\n"
		    "@display-name=Viewer;id=a2;user-id=8 :viewer!viewer@viewer.tmi.twitch.tv PRIVMSG #sarxina "
		    ":just chatting\r\n");
	tally.expect(waitFor([&] { return heard.chats == 2; }) && heard.lastChat == "just chatting",
		     "both lines reaching the plugin's feed of all of chat");
	tally.expect(heard.runs == 1 && heard.args == "from the socket" && heard.role == SARXINA_ROLE_MODERATOR &&
			     heard.display == "Mod",
		     "the plugin's handler got the command");
	twitch.send("@custom-reward-id=0c7c2a4e-6f1d-4a5b-9a43-2f8a1d6e5b77;display-name=Viewer;id=a3;user-id=8 "
		    ":viewer!viewer@viewer.tmi.twitch.tv PRIVMSG #sarxina :a redeem\r\n");
	tally.expect(waitFor([&] { return heard.chats == 3; }) && heard.lastChat == "a redeem" &&
			     heard.lastReward == "0c7c2a4e-6f1d-4a5b-9a43-2f8a1d6e5b77",
		     "a channel point reward reaching the feed with its id");

	twitch.send(":tmi.twitch.tv RECONNECT");
	tally.expect(waitFor([&] { return twitch.connections == 2; }), "connecting again when Twitch asks");
	handshake(" again");

	twitch.drop();
	tally.expect(waitFor([&] { return twitch.connections == 3; }), "coming back by itself after a drop");
	handshake(" after the drop");

	manager.followTwitchAccount("");
	tally.expect(manager.twitch().state() == TwitchChat::State::Off, "off when OBS has no Twitch account");
	return tally.report("Twitch connection", "steps as expected");
}

// ---- 5. VTube Studio ------------------------------------------------------------------------------------------------

struct Answer {
	int calls = 0;
	bool ok = false;
	QJsonObject data;
};

void answered(void *param, bool ok, const char *data)
{
	Answer &answer = *static_cast<Answer *>(param);
	answer.calls++;
	answer.ok = ok;
	answer.data = QJsonDocument::fromJson(data).object();
}

bool vtubeStudio()
{
	Tally tally;
	StandIn vts;
	Manager manager(Manager::Use::SelfTest);
	QJsonObject request;
	const auto state = [&manager] { return manager.vts().state(); };
	const auto type = [&request] { return request.value("messageType").toString(); };
	const auto data = [&request] { return request.value("data").toObject(); };

	// Connect pressed for the first time: VTube Studio asks the streamer, who allows it.
	manager.connectVts(vts.port());
	tally.expect(vts.nextJson(request) && type() == "AuthenticationTokenRequest" &&
			     data().value("pluginName").toString() == "Sarxina OBS Plugin Manager" &&
			     data().value("pluginDeveloper").toString() == "Sarxina",
		     "asking for a token as the Sarxina OBS Plugin Manager");
	tally.expect(state() == VTubeStudio::State::Allowing, "waiting for the streamer");
	vts.answer(request, "AuthenticationTokenResponse", {{"authenticationToken", "token-1"}});
	tally.expect(vts.nextJson(request) && type() == "AuthenticationRequest" &&
			     data().value("authenticationToken").toString() == "token-1",
		     "logging in with the new token");
	vts.answer(request, "AuthenticationResponse", {{"authenticated", true}});
	tally.expect(waitFor([&] { return state() == VTubeStudio::State::Ready; }), "connected");

	// A plugin's request, through the call plugins make.
	Answer answer;
	const sarxina_vts_request hotkeys{"HotkeysInCurrentModelRequest", "{\"modelID\":\"\"}", answered, &answer};
	manager.vtsRequest(hotkeys);
	tally.expect(vts.nextJson(request) && type() == "HotkeysInCurrentModelRequest" &&
			     request.value("apiName").toString() == "VTubeStudioPublicAPI" && data().contains("modelID"),
		     "the request reaching VTube Studio");
	vts.answer(request, "HotkeysInCurrentModelResponse",
		   {{"modelLoaded", true}, {"availableHotkeys", QJsonArray{QJsonObject{{"name", "Wave"}}}}});
	tally.expect(waitFor([&] { return answer.calls == 1; }) && answer.ok &&
			     answer.data.value("availableHotkeys").toArray().size() == 1,
		     "its answer reaching the plugin");

	Answer refused;
	const sarxina_vts_request trigger{"HotkeyTriggerRequest", "{\"hotkeyID\":\"nope\"}", answered, &refused};
	manager.vtsRequest(trigger);
	tally.expect(vts.nextJson(request), "a second request");
	vts.answer(request, "APIError", {{"errorID", 451}, {"message", "Hotkey not found."}});
	tally.expect(waitFor([&] { return refused.calls == 1; }) && !refused.ok &&
			     refused.data.value("message").toString() == "Hotkey not found.",
		     "an error answer reaching the plugin");

	Answer bad;
	const sarxina_vts_request broken{"HotkeyTriggerRequest", "not json", answered, &bad};
	manager.vtsRequest(broken);
	tally.expect(bad.calls == 0 && waitFor([&] { return bad.calls == 1; }) && !bad.ok, "data that is not JSON");

	manager.disconnectVts();
	Answer late;
	const sarxina_vts_request whileOff{"CurrentModelRequest", nullptr, answered, &late};
	manager.vtsRequest(whileOff);
	tally.expect(late.calls == 0 && waitFor([&] { return late.calls == 1; }) && !late.ok,
		     "a request while not connected, answered later and never inside the call");

	// The next time OBS starts: the remembered token, and no box. VTube Studio no longer takes it, so the manager
	// stops there rather than asking the streamer on its own.
	manager.vts().connectTo(vts.port(), "token-1", false);
	tally.expect(vts.nextJson(request) && type() == "AuthenticationRequest", "logging in with the remembered token");
	vts.answer(request, "AuthenticationResponse", {{"authenticated", false}});
	tally.expect(waitFor([&] { return state() == VTubeStudio::State::NotAllowed; }), "stopping when it is refused");
	QByteArray more;
	tally.expect(!vts.next(more, 500), "and not asking for a token on its own");

	// Connect pressed again, and the streamer says no.
	manager.connectVts(vts.port());
	tally.expect(vts.nextJson(request) && type() == "AuthenticationTokenRequest", "Connect asking again");
	vts.answer(request, "APIError", {{"errorID", 50}, {"message", "User has denied API access for your plugin."}});
	tally.expect(waitFor([&] { return state() == VTubeStudio::State::NotAllowed; }), "a no taken as a no");
	manager.disconnectVts();

	return tally.report("VTube Studio", "steps as expected");
}

// ---- the picture ----------------------------------------------------------------------------------------------------

void picture(const char *folder)
{
	if (!folder || !*folder || !QDir().mkpath(QString::fromUtf8(folder)))
		return;
	StandIn twitch;
	Heard first, second;
	const sarxina_plugin_info one = testPlugin(first, "first", "Test plugin");
	sarxina_plugin_info two = testPlugin(second, "second", "Another test plugin");
	two.commands = nullptr;
	two.command_count = 0;
	two.get_properties = nullptr;
	two.description = "Switched off, and with no settings.";

	Manager manager(Manager::Use::SelfTest, twitch.url());
	ManagerDock dock(manager); // made first, as in OBS, where plugins register once the dock is there
	manager.add(&one);
	manager.add(&two);
	manager.setEnabled(*manager.plugins()[0], true); // the second one is left not added
	dock.setAttribute(Qt::WA_DontShowOnScreen);
	dock.resize(380, 520);
	dock.show();
	dock.findChildren<QCheckBox *>().at(1)->setChecked(false); // the second plugin's description, unfolded

	manager.followTwitchAccount("sarxina");
	QByteArray cap, nick, join;
	twitch.next(cap), twitch.next(nick), twitch.next(join);
	twitch.send(":" + nick.mid(5) + "!x@x JOIN #sarxina");
	waitFor([&manager] { return manager.twitch().state() == TwitchChat::State::Reading; });

	const QString file = QDir(QString::fromUtf8(folder)).filePath(QStringLiteral("dock.png"));
	test::report(dock.grab().save(file, "PNG"), "a picture of the dock in %s", file.toUtf8().constData());
	manager.followTwitchAccount("");

	// What the gear shows: the settings the plugin describes, drawn by the manager.
	QWidget settings;
	settings.setAttribute(Qt::WA_DontShowOnScreen);
	auto *layout = new QVBoxLayout(&settings);
	layout->addWidget(new PropertiesForm(*manager.plugins()[0], &settings));
	settings.resize(420, settings.sizeHint().height());
	settings.show();
	waitFor([] { return false; }, 200);
	const QString gear = QDir(QString::fromUtf8(folder)).filePath(QStringLiteral("settings.png"));
	test::report(settings.grab().save(gear, "PNG"), "a picture of a plugin's settings in %s", gear.toUtf8().constData());

	// A long list: twenty more plugins, one line each, in a dock that scrolls.
	std::deque<std::string> names;
	std::deque<Heard> heard(20);
	std::deque<sarxina_plugin_info> more;
	for (int i = 0; i < 20; i++) {
		names.push_back("plugin-" + std::to_string(i + 3));
		names.push_back("Plugin " + std::to_string(i + 3));
		more.push_back(testPlugin(heard[i], names[names.size() - 2].c_str(), names.back().c_str()));
		more.back().commands = nullptr;
		more.back().command_count = 0;
		manager.add(&more.back());
	}
	dock.resize(380, 720);
	waitFor([] { return false; }, 200);
	const QString many = QDir(QString::fromUtf8(folder)).filePath(QStringLiteral("dock-22-plugins.png"));
	test::report(dock.grab().save(many, "PNG"), "a picture of the dock with 22 plugins in %s",
		     many.toUtf8().constData());
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	bool passed = parsing();
	passed &= account();
	passed &= checks();
	passed &= routing();
	passed &= twitchConnection();
	passed &= vtubeStudio();
	picture(picturesFolder);
	test::report(passed, "%s", passed ? "passed" : "FAILED");
	return passed;
}

// Inside OBS: what registered through OBS's procedure handler, and one read-only request to the real VTube Studio,
// sent the way a plugin sends it, once VTube Studio is connected.
void checkInObs(Manager &manager)
{
	std::string names;
	for (const auto &plugin : manager.plugins())
		names += (names.empty() ? "" : ", ") + std::string(plugin->info->name) +
			 (plugin->enabled ? " (added)" : " (not added)");
	test::report(!names.empty(), "registered through OBS: %s", names.empty() ? "nothing" : names.c_str());

	static const sarxina_vts_request request{
		"CurrentModelRequest", nullptr,
		[](void *, bool ok, const char *data) {
			const QJsonObject answer = QJsonDocument::fromJson(data).object();
			const QByteArray model = answer.value("modelName").toString().toUtf8();
			if (!ok)
				test::report(false, "VTube Studio, asked the way a plugin asks: %s", data);
			else if (answer.value("modelLoaded").toBool())
				test::report(true, "VTube Studio, asked the way a plugin asks: model loaded: %s", model.constData());
			else
				test::report(true, "VTube Studio, asked the way a plugin asks: no model loaded");
		},
		nullptr};
	const auto ask = [] {
		test::report(sarxina_obs_plugin_manager_vts_request(&request), "a VTube Studio request sent through OBS");
	};
	if (manager.vts().state() == VTubeStudio::State::Ready) {
		ask();
		return;
	}
	test::report(true, "VTube Studio is not connected yet; the request goes once it is");
	auto *once = new QMetaObject::Connection;
	*once = QObject::connect(&manager.vts(), &VTubeStudio::stateChanged, &manager, [&manager, once, ask] {
		if (manager.vts().state() != VTubeStudio::State::Ready)
			return;
		QObject::disconnect(*once);
		delete once;
		ask();
	});
}

