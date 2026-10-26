// The Sarxina OBS Plugin Manager's self-test (see shared/src/self-test.hpp). It needs no OBS and no internet: Twitch
// and VTube Studio are played by small stand-in websocket servers on this computer, and GitHub and Twitch's API by
// stand-in http servers, and plugins are installed into a folder of its own that it deletes afterwards.
//
//   1. Lines from Twitch become the right chat messages.
//   2. The checks drop what they should and let the rest through.
//   3. Commands reach the plugin that registered them, and only when they may.
//   4. The whole way: a stand-in Twitch sends chat over a real websocket, and a plugin's handler gets the command.
//   5. VTube Studio over a real websocket: allowing the manager once, logging in with the remembered token, never
//      asking again on its own, and a plugin's request and its answer, also a long one in many frames.
//   6. The list of plugins: only zips on Sarxina's releases, over HTTPS, with a SHA-256; and only GitHub's own hosts
//      for its redirects.
//   7. Zips: unpacked whole, every file checked, and nothing but the plugin's own files written.
//   8. Installing, updating and uninstalling, laid out the way a portable OBS and ProgramData want them, with
//      downloads that go wrong installing nothing, and what is left for the next start finished then.
//   9. The dock: Install, Uninstall and Update, what each line says, and the list of plugins OBS runs.
//  10. The one Twitch connection, against a stand-in for Twitch that answers as Twitch's documentation shows:
//      connecting with the device code flow, plugins' requests with the broadcaster, the Client-Id and the token added,
//      Twitch's answers passed on as they came, a token refreshed once when it ran out, and every way it can go wrong.
//  11. The dock's line for Twitch in each of its states, and the chat of the account connected under it.
//  12. Buttons in a plugin's settings.
// Given a folder, it also paints the dock into dock.png there (and into dock-code.png while the code for Twitch waits
// to be entered), its dropdown into dropdown.png, and what a plugin's gear shows into settings.png. With
// SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_ONLINE=1 it also downloads a released zip from GitHub itself.
//
// Inside OBS (SARXINA_OBS_PLUGIN_MANAGER_SELFTEST=1) checkInObs also lists the plugins that registered through OBS
// and, once VTube Studio is connected, sends it one request that only reads, the way a plugin would; and when Twitch is
// connected, asks Twitch for the channel's rewards the same way. pressInObs presses one button in the real dock
// (SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PRESS, see there).
#include "self-test.hpp"

#include "catalog.hpp"
#include "fetch.hpp"
#include "installer.hpp"
#include "manager-dock.hpp"
#include "manager.hpp"
#include "properties-form.hpp"
#include "store.hpp"
#include "zip.hpp"

#include <zlib.h>

#include <QCoreApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace sarxina;
using Result = ChatRouter::Result;

namespace {

// ---- helpers --------------------------------------------------------------------------------------------------------

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// For a manager whose chat is not under test: nothing listens there, so it never reaches the real Twitch.
const QString kNoChat = QStringLiteral("ws://127.0.0.1:1");

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

	// A whole text message in one frame, or, given a size, split into frames of that size, the way VTube Studio sends
	// a long one.
	void send(const QByteArray &text, qsizetype pieces = 0)
	{
		const qsizetype size = pieces > 0 ? pieces : std::max<qsizetype>(text.size(), 1);
		for (qsizetype at = 0; at < text.size() || at == 0; at += size) {
			const QByteArray piece = text.mid(at, size);
			const bool first = at == 0, last = at + size >= text.size();
			// FIN on the last frame; text on the first, continuation on the rest
			QByteArray frame(1, static_cast<char>((last ? 0x80 : 0x00) | (first ? 0x01 : 0x00)));
			if (piece.size() < 126) {
				frame += char(piece.size());
			} else {
				frame += char(126);
				frame += char((piece.size() >> 8) & 0xFF);
				frame += char(piece.size() & 0xFF);
			}
			frame += piece;
			if (client_) {
				client_->write(frame);
				client_->flush();
			}
		}
	}
	// VTube Studio's answer to a request.
	void answer(const QJsonObject &request, const char *type, const QJsonObject &data, qsizetype pieces = 0)
	{
		send(QJsonDocument(QJsonObject{{"apiName", "VTubeStudioPublicAPI"},
					       {"apiVersion", "1.0"},
					       {"requestID", request.value("requestID")},
					       {"messageType", type},
					       {"data", data}})
			     .toJson(QJsonDocument::Compact),
		     pieces);
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
	tally.expect(manager.add(&info) && heard.enabled, "a plugin registered, and on");

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
	tally.expect(manager.twitch().state() == TwitchChat::State::Off, "off when no Twitch account is connected");
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

	// A long answer, split into frames of 1016 bytes as VTube Studio sends it, with braces inside its strings.
	Answer longAnswer;
	const sarxina_vts_request again{"HotkeysInCurrentModelRequest", nullptr, answered, &longAnswer};
	manager.vtsRequest(again);
	QJsonArray many;
	for (int i = 0; i < 60; i++)
		many.append(QJsonObject{{"name", QStringLiteral("Hotkey {%1] \"x\"").arg(i)}, {"hotkeyID", QString::number(i)}});
	tally.expect(vts.nextJson(request) && type() == "HotkeysInCurrentModelRequest" && !request.contains("data"),
		     "a request without data");
	vts.answer(request, "HotkeysInCurrentModelResponse", {{"modelLoaded", true}, {"availableHotkeys", many}}, 1016);
	tally.expect(waitFor([&] { return longAnswer.calls == 1; }) && longAnswer.ok &&
			     longAnswer.data.value("availableHotkeys").toArray().size() == 60,
		     "a long answer in many frames reaching the plugin whole");

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

// ---- 6. the list of plugins -----------------------------------------------------------------------------------------

QByteArray sha(const QByteArray &bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QJsonObject listed(const QString &id, const QString &version, const QString &url, const QByteArray &sha256)
{
	return {{"id", id},
		{"name", id.toUpper()},
		{"description", "Only here for the self-test."},
		{"version", version},
		{"url", url},
		{"sha256", QString::fromLatin1(sha256)}};
}

QByteArray listOf(const QJsonArray &plugins)
{
	return QJsonDocument(QJsonObject{{"plugins", plugins}}).toJson(QJsonDocument::Compact);
}

bool catalog()
{
	Tally tally;
	const Source github = Source::github();
	tally.expect(github.index == "https://raw.githubusercontent.com/Sarxina/sarxina-obs-plugins/main/plugins.json" &&
			     github.downloads == "https://github.com/Sarxina/sarxina-obs-plugins/releases/download/" &&
			     !github.local,
		     "GitHub: Sarxina's repository, over HTTPS");
	const QByteArray hash(64, 'a');
	const QString release = github.downloads + "slinglive-v1.3.0/slinglive-1.3.0-windows-x64.zip";
	const struct {
		const char *what;
		QJsonObject entry;
		bool kept;
	} cases[] = {
		{"a plugin as released", listed("slinglive", "1.3.0", release, hash), true},
		{"a zip on another repository",
		 listed("a", "1.0.0", "https://github.com/someone/else/releases/download/v1/a.zip", hash), false},
		{"a zip over http", listed("b", "1.0.0", "http://github.com/Sarxina/sarxina-obs-plugins/releases/download/t/b.zip", hash),
		 false},
		{"a way out of the releases with ..", listed("c", "1.0.0", github.downloads + "../../../evil/c.zip", hash), false},
		{"a way out with %2e", listed("d", "1.0.0", github.downloads + "%2e%2e/x/d.zip", hash), false},
		{"a query", listed("e", "1.0.0", github.downloads + "t/e.zip?x=1", hash), false},
		{"something other than a zip", listed("f", "1.0.0", github.downloads + "t/f.exe", hash), false},
		{"no SHA-256", listed("g", "1.0.0", github.downloads + "t/g.zip", ""), false},
		{"an upper-case SHA-256", listed("h", "1.0.0", github.downloads + "t/h.zip", QByteArray(64, 'A')), false},
		{"a version that is not x.y.z", listed("i", "1.0", github.downloads + "t/i.zip", hash), false},
		{"an id that is not a module name", listed("../j", "1.0.0", github.downloads + "t/j.zip", hash), false},
		{"the manager itself",
		 listed("sarxina-obs-plugin-manager", "1.0.0", github.downloads + "t/m.zip", hash), false},
		{"the same plugin again", listed("slinglive", "1.3.1", release, hash), false},
	};
	QJsonArray entries;
	for (const auto &c : cases)
		entries.append(c.entry);
	std::vector<Available> list;
	QStringList problems;
	tally.expect(readIndex(listOf(entries), github, list, problems), "a list read");
	tally.expect(list.size() == 1 && list[0].id == "slinglive" && list[0].name == "SLINGLIVE" &&
			     list[0].version == "1.3.0" && list[0].url == release && list[0].sha256 == hash,
		     "only the plugin as released kept");
	tally.expect(problems.size() == std::size(cases) - 1, "every other one named as left out");
	tally.expect(!readIndex("not json", github, list, problems) && list.empty() &&
			     !readIndex("{\"plugins\":{}}", github, list, problems),
		     "a file that is not a list");

	tally.expect(newer("1.10.0", "1.9.2") && newer("2.0.0", "1.99.99") && !newer("1.3.0", "1.3.0") &&
			     !newer("1.2.9", "1.3.0") && !newer("1.4.0", "") && !newer("x", "1.0.0"),
		     "which version is newer");

	const auto test = Source::testServer("http://127.0.0.1:8790");
	tally.expect(test && test->local && test->index == "http://127.0.0.1:8790/plugins.json" &&
			     test->downloads == "http://127.0.0.1:8790/",
		     "a test server on this computer");
	tally.expect(Source::testServer("http://localhost:1/") && Source::testServer("http://[::1]:2"),
		     "localhost and ::1 too");
	for (const char *other : {"http://192.168.1.5:8790", "http://example.com:80", "https://127.0.0.1:8790",
				  "http://127.0.0.1", "http://user@127.0.0.1:1", "http://127.0.0.1:1/path", "127.0.0.1:1"})
		tally.expect(!Source::testServer(other), std::string("not a test server: ") + other);

	tally.expect(Fetcher::githubHost("https://github.com/Sarxina/x") &&
			     Fetcher::githubHost("https://release-assets.githubusercontent.com/a?b=c") &&
			     Fetcher::githubHost("https://objects.githubusercontent.com/a"),
		     "GitHub's download hosts");
	for (const char *other : {"http://objects.githubusercontent.com/a", "https://githubusercontent.com.evil.example/a",
				  "https://evil.example/github.com", "https://github.com.evil.example/",
				  "https://user@github.com/", "https://github.com:8443/"})
		tally.expect(!Fetcher::githubHost(other), std::string("not GitHub: ") + other);
	return tally.report("list of plugins", "checks as expected");
}

// ---- 7. zips --------------------------------------------------------------------------------------------------------

struct Packed {
	QString name;
	QByteArray data;
};

// A zip as the release zips are made, its files stored or deflated, with the folder entries before them.
QByteArray makeZip(const std::vector<Packed> &files, bool deflate)
{
	const auto u16 = [](QByteArray &out, uint32_t v) { out.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF)); };
	const auto u32 = [&u16](QByteArray &out, uint32_t v) {
		u16(out, v & 0xFFFF);
		u16(out, v >> 16);
	};
	QByteArray zip, directory;
	uint32_t count = 0;
	const auto add = [&](const QByteArray &name, const QByteArray &data) {
		QByteArray packed = data;
		const bool folder = name.endsWith('/');
		const bool deflated = deflate && !folder;
		if (deflated) {
			z_stream stream{};
			deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
			packed.resize(static_cast<qsizetype>(deflateBound(&stream, static_cast<uLong>(data.size()))));
			stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
			stream.avail_in = static_cast<uInt>(data.size());
			stream.next_out = reinterpret_cast<Bytef *>(packed.data());
			stream.avail_out = static_cast<uInt>(packed.size());
			::deflate(&stream, Z_FINISH);
			packed.resize(static_cast<qsizetype>(stream.total_out));
			deflateEnd(&stream);
		}
		const uint32_t crc = static_cast<uint32_t>(
			crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.constData()), static_cast<uInt>(data.size())));
		const uint32_t offset = static_cast<uint32_t>(zip.size());
		// The local header without sizes (bit 3: they follow the data), as libarchive writes them
		u32(zip, 0x04034b50), u16(zip, 20), u16(zip, 8), u16(zip, deflated ? 8 : 0), u16(zip, 0), u16(zip, 0);
		u32(zip, 0), u32(zip, 0), u32(zip, 0), u16(zip, static_cast<uint32_t>(name.size())), u16(zip, 0);
		zip += name;
		zip += packed;
		u32(zip, 0x08074b50), u32(zip, crc), u32(zip, static_cast<uint32_t>(packed.size())),
			u32(zip, static_cast<uint32_t>(data.size()));
		u32(directory, 0x02014b50), u16(directory, 0x031E), u16(directory, 20), u16(directory, 8),
			u16(directory, deflated ? 8 : 0), u16(directory, 0), u16(directory, 0), u32(directory, crc);
		u32(directory, static_cast<uint32_t>(packed.size())), u32(directory, static_cast<uint32_t>(data.size()));
		u16(directory, static_cast<uint32_t>(name.size())), u16(directory, 0), u16(directory, 0), u16(directory, 0);
		u16(directory, 0), u32(directory, 0), u32(directory, offset);
		directory += name;
		count++;
	};
	QStringList folders;
	for (const Packed &file : files) {
		const QStringList parts = file.name.split('/');
		for (int i = 1; i < parts.size(); i++) {
			const QString folder = parts.mid(0, i).join('/') + '/';
			if (!folders.contains(folder)) {
				folders << folder;
				add(folder.toLatin1(), {});
			}
		}
		add(file.name.toLatin1(), file.data);
	}
	const uint32_t at = static_cast<uint32_t>(zip.size());
	zip += directory;
	u32(zip, 0x06054b50), u16(zip, 0), u16(zip, 0), u16(zip, count), u16(zip, count);
	u32(zip, static_cast<uint32_t>(directory.size())), u32(zip, at), u16(zip, 0);
	return zip;
}

// A plugin's zip as released: its DLL (only bytes here, never loaded), its strings and its licence.
QByteArray pluginZip(const QString &id, const QByteArray &dll, bool deflate = true)
{
	return makeZip({{"obs-plugins/64bit/" + id + ".dll", dll},
			{"data/obs-plugins/" + id + "/LICENSE.txt", QByteArray(3000, 'L')},
			{"data/obs-plugins/" + id + "/locale/en-US.ini", "Description=\"Only here for the self-test.\"\n"}},
		       deflate);
}

QByteArray readFile(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("(none)");
}

bool zips()
{
	Tally tally;
	const QByteArray dll = QByteArray("MZ") + QByteArray(70000, '\x01') + "the end";
	std::vector<ZipFile> files;
	QString why;
	for (const bool deflate : {true, false}) {
		const char *how = deflate ? " (deflated)" : " (stored)";
		tally.expect(readZip(pluginZip("one", dll, deflate), files, why), std::string("a plugin's zip read") + how);
		tally.expect(files.size() == 3 && files[0].name == "obs-plugins/64bit/one.dll" && files[0].data == dll &&
				     files[2].name == "data/obs-plugins/one/locale/en-US.ini",
			     std::string("its three files, without the folders") + how);
	}

	// One byte of the DLL changed after its CRC was written
	QByteArray changed = pluginZip("one", dll, false);
	changed[changed.indexOf("the end")] = 'T';
	tally.expect(!readZip(changed, files, why) && files.empty(), "a file that is not what the zip says");
	const QByteArray whole = pluginZip("one", dll);
	tally.expect(!readZip(whole.left(whole.size() - 30), files, why) && !readZip("PK", files, why) &&
			     !readZip(QByteArray(100, '\0'), files, why),
		     "a cut-off zip and things that are no zip");

	// What may be in a plugin's zip: obs-plugins/64bit/<id>.dll and data/obs-plugins/<id>/..., nothing else.
	QTemporaryDir folder;
	const Layout layout{Layout::Kind::ObsFolder, folder.path()};
	const auto refused = [&](const std::vector<Packed> &packed, const std::string &what) {
		std::vector<ZipFile> unpacked;
		QString problem;
		tally.expect(readZip(makeZip(packed, true), unpacked, problem) &&
				     !files::write(layout, "one", unpacked, false, problem) &&
				     QDir(folder.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty(),
			     "refused, nothing written: " + what);
	};
	const Packed theDll{"obs-plugins/64bit/one.dll", dll};
	refused({theDll, {"data/obs-plugins/one/../../../escaped.txt", "x"}}, "a way out with ..");
	refused({theDll, {"data/obs-plugins/one/../two/x.ini", "x"}}, "another plugin's data");
	refused({theDll, {"obs-plugins/64bit/other.dll", "MZ"}}, "a second DLL");
	refused({theDll, {"bin/64bit/obs64.exe", "MZ"}}, "a file of OBS's");
	refused({theDll, {"data/obs-plugins/one/x:stream", "x"}}, "a stream name");
	refused({theDll, {"data/obs-plugins/one/trailing.", "x"}}, "a name Windows would change");
	refused({{"data/obs-plugins/one/locale/en-US.ini", "x"}}, "no DLL");
	refused({theDll, {"Obs-Plugins/64bit/one.dll", "MZ"}}, "the DLL twice");
	return tally.report("zips", "checks as expected");
}

// ---- 8. installing, over a stand-in for GitHub ----------------------------------------------------------------------

// An http server on this computer standing in for GitHub: it answers each GET from a table of paths.
class StandInGitHub : public QObject {
public:
	struct Answer {
		int status = 200;
		QByteArray body;
		QByteArray location; // for a redirect
	};

	StandInGitHub()
	{
		server_.listen(QHostAddress::LocalHost, 0);
		connect(&server_, &QTcpServer::newConnection, this, [this] {
			while (QTcpSocket *socket = server_.nextPendingConnection()) {
				auto *buffer = new QByteArray;
				connect(socket, &QTcpSocket::disconnected, socket, [socket, buffer] {
					delete buffer;
					socket->deleteLater();
				});
				connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
					*buffer += socket->readAll();
					if (!buffer->contains("\r\n\r\n"))
						return;
					const QByteArray path = buffer->split(' ').value(1);
					buffer->clear();
					requests.push_back(path);
					const auto found = answers.find(path);
					const Answer answer = found != answers.end() ? found->second : Answer{404, "not here", {}};
					QByteArray reply = "HTTP/1.1 " + QByteArray::number(answer.status) + " X\r\nContent-Length: " +
							   QByteArray::number(answer.body.size()) + "\r\nConnection: close\r\n";
					if (!answer.location.isEmpty())
						reply += "Location: " + answer.location + "\r\n";
					socket->write(reply + "\r\n" + answer.body);
					socket->disconnectFromHost();
				});
			}
		});
	}

	Source source() const { return *Source::testServer(QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort())); }
	QString url(const char *path) const { return source().downloads + path; }

	std::map<QByteArray, Answer> answers;
	std::vector<QByteArray> requests;

private:
	QTcpServer server_;
};

// What OBS loaded, as the self-test says: plugin id -> what OBS did with it.
using Pretend = std::map<QString, Store::Loaded>;

Store::Setup setupFor(const StandInGitHub &github, const Layout &layout, const Pretend *loaded, const QString &remembered)
{
	Store::Setup setup;
	setup.source = github.source();
	setup.layout = layout;
	setup.remembered = remembered;
	if (loaded)
		setup.loaded = [loaded](const QString &id) {
			const auto found = loaded->find(id);
			return found != loaded->end() ? found->second : Store::Loaded();
		};
	return setup;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool installing()
{
	Tally tally;
	StandInGitHub github;
	const QByteArray oneDll = QByteArray("MZ one ") + QByteArray(5000, 'x');
	const QByteArray twoDll = QByteArray("MZ two 1.1.0 ") + QByteArray(5000, 'y');
	const QByteArray oneZip = pluginZip("one", oneDll), twoZip = pluginZip("two", twoDll);
	github.answers["/one.zip"] = {200, oneZip, {}};
	github.answers["/two.zip"] = {200, twoZip, {}};
	github.answers["/wrong.zip"] = {200, oneZip, {}};
	github.answers["/away.zip"] = {302, {}, "https://github.com/Sarxina/sarxina-obs-plugins/releases/download/t/a.zip"};
	github.answers["/big.zip"] = {200, QByteArray(kLargestZip + 1, 'B'), {}};
	github.answers["/notazip.zip"] = {200, QByteArray("not a zip"), {}};
	github.answers["/plugins.json"] = {
		200,
		listOf({listed("one", "1.0.0", github.url("one.zip"), sha(oneZip)),
			listed("two", "1.1.0", github.url("two.zip"), sha(twoZip)),
			listed("wrong", "1.0.0", github.url("wrong.zip"), QByteArray(64, '0')),
			listed("gone", "1.0.0", github.url("gone.zip"), sha(oneZip)),
			listed("away", "1.0.0", github.url("away.zip"), sha(oneZip)),
			listed("big", "1.0.0", github.url("big.zip"), sha(oneZip)),
			listed("notazip", "1.0.0", github.url("notazip.zip"), sha("not a zip")),
			listed("elsewhere", "1.0.0", github.url("one.zip"), sha(oneZip))}),
		{}};

	for (const Layout::Kind kind : {Layout::Kind::ObsFolder, Layout::Kind::ProgramData}) {
		const bool portable = kind == Layout::Kind::ObsFolder;
		const std::string in = portable ? " (portable OBS)" : " (ProgramData)";
		QTemporaryDir folder;
		const Layout layout{kind, folder.path()};
		const QString remembered = folder.filePath("config/store.json");
		Pretend loaded;
		// "two" 1.0.0 runs from where the manager installs; "elsewhere" was put into OBS's own folder by hand.
		const QString twoDllPath = layout.dll("two");
		writeFile(twoDllPath, "MZ two 1.0.0");
		writeFile(layout.data("two") + "/locale/en-US.ini", "old");
		loaded["two"] = {Store::Loaded::State::Yes, twoDllPath, "1.0.0"};
		loaded["elsewhere"] = {Store::Loaded::State::Yes, folder.filePath("Program Files/obs-plugins/64bit/elsewhere.dll"),
				       "1.0.0"};

		auto store = std::make_unique<Store>(setupFor(github, layout, &loaded, remembered));
		std::vector<std::pair<QString, bool>> switched;
		QObject::connect(store.get(), &Store::switchPlugin,
				 [&switched](const QString &id, bool on) { switched.emplace_back(id, on); });
		const auto settled = [&store](const QString &id) {
			return waitFor([&] { return !store->state(id).busy; }, 10000);
		};

		tally.expect(store->list() == Store::List::Waiting, "waiting for the list" + in);
		store->start();
		tally.expect(waitFor([&] { return store->list() == Store::List::Ready; }) && store->available().size() == 8,
			     "the list fetched" + in);

		// Installing one OBS is not running
		Store::State one = store->state("one");
		tally.expect(!one.installed && !one.busy && one.note.isEmpty() && !store->restartNeeded(), "one: not installed" + in);
		store->install("one");
		tally.expect(store->state("one").busy, "one: downloading" + in);
		tally.expect(settled("one"), "one: downloaded" + in);
		one = store->state("one");
		tally.expect(one.installed && one.change == Store::Change::Installed && one.note.isEmpty() &&
				     store->restartNeeded(),
			     "one: installed, waiting for a restart" + in);
		tally.expect(readFile(layout.dll("one")) == oneDll &&
				     readFile(layout.data("one") + "/locale/en-US.ini").startsWith("Description=") &&
				     files::exists(layout.data("one") + "/LICENSE.txt") && !files::exists(layout.dll("one") + ".part"),
			     "one: its files where OBS loads them" + in);
		tally.expect(portable ? layout.dll("one") == folder.path() + "/obs-plugins/64bit/one.dll"
				      : layout.dll("one") == folder.path() + "/one/bin/64bit/one.dll" &&
						layout.data("one") == folder.path() + "/one/data",
			     "one: laid out the way OBS looks for it" + in);
		store->uninstall("one");
		one = store->state("one");
		tally.expect(!one.installed && one.change == Store::Change::None && !files::exists(layout.dll("one")) &&
				     !files::exists(layout.data("one")) && !store->restartNeeded(),
			     "one: uninstalled before OBS ever ran it, at once" + in);
		tally.expect(portable || !files::exists(layout.folder("one")), "one: its own folder gone too" + in);

		// Downloads that go wrong install nothing
		const auto fails = [&](const char *id, const char *problemKey, const std::string &what) {
			store->install(id);
			settled(id);
			const Store::State state = store->state(id);
			const QString expected = text(problemKey).left(20);
			tally.expect(!state.installed && state.change == Store::Change::None && state.note.startsWith(expected) &&
					     !files::exists(layout.dll(id)) && !files::exists(layout.dll(id) + ".part"),
				     what + ", nothing written" + in);
		};
		fails("wrong", "Problem.Checksum", "a zip whose SHA-256 does not match");
		fails("gone", "Problem.Download", "a zip that is not there");
		fails("away", "Problem.Download", "a test server sending it elsewhere");
		fails("big", "Problem.Download", "a zip that is too big");
		fails("notazip", "Problem.Zip", "a download that is not a zip");

		// Updating "two" while OBS runs 1.0.0
		Store::State two = store->state("two");
		tally.expect(two.installed && two.canUpdate && two.newVersion == "1.1.0" && store->running("two"),
			     "two: running, with an update" + in);
		store->update("two");
		settled("two");
		two = store->state("two");
		tally.expect(two.installed && !two.canUpdate && two.change == Store::Change::Updated &&
				     readFile(twoDllPath) == twoDll && readFile(twoDllPath + ".old") == "MZ two 1.0.0",
			     "two: updated, the running DLL moved aside" + in);

		// Uninstalling it while OBS runs it, taking that back, and uninstalling it again
		store->uninstall("two");
		two = store->state("two");
		tally.expect(!two.installed && two.change == Store::Change::Uninstalled && !store->running("two") &&
				     !files::exists(twoDllPath) && readFile(twoDllPath + ".uninstalled") == "MZ two 1.0.0" &&
				     switched.size() == 1 && switched[0] == std::make_pair(QString("two"), false),
			     "two: uninstalled, its running DLL renamed, switched off" + in);
		store->install("two");
		two = store->state("two");
		tally.expect(two.installed && two.change == Store::Change::None && store->running("two") &&
				     readFile(twoDllPath) == "MZ two 1.0.0" && switched.size() == 2 && switched[1].second &&
				     !store->restartNeeded(),
			     "two: the uninstall taken back, on again" + in);
		store->uninstall("two");
		tally.expect(store->state("two").change == Store::Change::Uninstalled && store->restartNeeded(),
			     "two: uninstalled again" + in);

		// One that was put into OBS's own folder by hand is left alone
		store->uninstall("elsewhere");
		const Store::State elsewhere = store->state("elsewhere");
		tally.expect(elsewhere.installed && elsewhere.change == Store::Change::None &&
				     elsewhere.note.startsWith(text("Problem.ByHand").left(20)) && !elsewhere.canUpdate,
			     "a plugin installed by hand elsewhere: not touched, and why" + in);

		// The next time OBS starts: "two" did not load, and what the uninstall left is deleted.
		store.reset();
		loaded.erase("two");
		store = std::make_unique<Store>(setupFor(github, layout, &loaded, remembered));
		tally.expect(!files::exists(twoDllPath + ".uninstalled") && !files::exists(twoDllPath + ".old") &&
				     !files::exists(layout.data("two")) && (portable || !files::exists(layout.folder("two"))),
			     "next start: what the uninstall left deleted" + in);
		tally.expect(!store->state("two").installed && !store->restartNeeded(), "next start: two not installed" + in);
		tally.expect(store->list() == Store::List::Waiting, "next start: a test server's list never kept" + in);
		store->start();
		waitFor([&] { return store->list() == Store::List::Ready; });

		// An update left for the next start
		writeFile(twoDllPath, "MZ two 1.0.0");
		loaded["two"] = {Store::Loaded::State::Yes, twoDllPath, "1.0.0"};
		store.reset();
		store = std::make_unique<Store>(setupFor(github, layout, &loaded, remembered));
		store->start();
		waitFor([&] { return store->list() == Store::List::Ready; });
		store->update("two");
		settled("two");
		tally.expect(files::exists(twoDllPath + ".old"), "an update before a restart" + in);
		store.reset();
		loaded["two"].version = "1.1.0";
		store = std::make_unique<Store>(setupFor(github, layout, &loaded, remembered));
		tally.expect(!files::exists(twoDllPath + ".old") && readFile(twoDllPath) == twoDll, "next start: the old DLL deleted" + in);
		store->start();
		waitFor([&] { return store->list() == Store::List::Ready; });
		tally.expect(!store->state("two").canUpdate && store->state("two").installed, "next start: up to date" + in);

		// Installed, but OBS did not load it
		store->install("one");
		settled("one");
		store.reset();
		store = std::make_unique<Store>(setupFor(github, layout, &loaded, remembered));
		store->start();
		const Store::State notLoaded = store->state("one");
		tally.expect(notLoaded.installed && notLoaded.note == text("Note.NotLoaded"), "installed, but not loaded: said so" + in);
		store->uninstall("one");
		tally.expect(!files::exists(layout.dll("one")), "and it can be uninstalled at once" + in);
	}

	// While OBS is still loading plugins, one loaded after the manager counts once it is there, and from the end of
	// loading on, what OBS loaded is noted for good.
	{
		QTemporaryDir folder;
		Pretend later;
		Store store(setupFor(github, {Layout::Kind::ObsFolder, folder.path()}, &later, {}));
		tally.expect(!store.state("three").installed, "a plugin OBS has not loaded yet");
		later["three"] = {Store::Loaded::State::Yes, folder.filePath("obs-plugins/64bit/three.dll"), "1.0.0"};
		tally.expect(store.state("three").installed && store.running("three"), "the same once OBS has loaded it");
		// As the dock does: every line shown again once OBS has finished loading
		QObject::connect(&store, &Store::listChanged, [&store] { store.state("three"); });
		store.start();
		later.erase("three");
		tally.expect(store.state("three").installed, "noted once OBS has finished loading");
	}

	// No list: GitHub cannot be reached
	{
		QTemporaryDir folder;
		StandInGitHub nothing;
		Store::Setup setup = setupFor(nothing, {Layout::Kind::ObsFolder, folder.path()}, nullptr, {});
		setup.source = *Source::testServer("http://127.0.0.1:9"); // nothing listens there
		Store store(setup);
		store.start();
		tally.expect(waitFor([&] { return store.list() == Store::List::Failed; }, 15000) && store.available().empty(),
			     "no list when the server cannot be reached");
	}
	return tally.report("installing", "steps as expected");
}

// ---- 9. the dock ----------------------------------------------------------------------------------------------------

// The dropdown with every plugin.
QMenu *dropdown(ManagerDock &dock)
{
	for (QToolButton *button : dock.findChildren<QToolButton *>())
		if (button->menu())
			return button->menu();
	return nullptr;
}

// A plugin's line in the dropdown, and what is on it.
QWidget *choice(ManagerDock &dock, const QString &id)
{
	QMenu *menu = dropdown(dock);
	return menu ? menu->findChild<QWidget *>("plugin:" + id) : nullptr;
}

QString button(ManagerDock &dock, const QString &id, const char *which = "install")
{
	QWidget *row = choice(dock, id);
	QPushButton *button = row ? row->findChild<QPushButton *>(which) : nullptr;
	return !button ? QStringLiteral("(none)") : button->isHidden() ? QStringLiteral("(hidden)") : button->text();
}

QString note(ManagerDock &dock, const QString &id)
{
	QWidget *row = choice(dock, id);
	QLabel *label = row ? row->findChild<QLabel *>("note") : nullptr;
	return !label || label->isHidden() ? QString() : label->text();
}

void press(ManagerDock &dock, const QString &id, const char *which = "install")
{
	if (QWidget *row = choice(dock, id))
		if (QPushButton *button = row->findChild<QPushButton *>(which))
			button->click();
}

// A plugin's line in the list, by its name, and whether it shows.
QWidget *line(ManagerDock &dock, const char *name)
{
	for (QCheckBox *title : dock.findChildren<QCheckBox *>())
		if (title->property("class").toString() == "indicator-expand" && title->text() == name)
			return title->parentWidget();
	return nullptr;
}

bool shown(ManagerDock &dock, const char *name)
{
	QWidget *found = line(dock, name);
	return found && !found->isHidden();
}

bool hasGear(ManagerDock &dock, const char *name)
{
	QWidget *found = line(dock, name);
	QToolButton *gear = found ? found->findChild<QToolButton *>() : nullptr;
	return gear && !gear->isHidden();
}

QLabel *dockSays(ManagerDock &dock, const QString &words)
{
	for (QLabel *label : dock.findChildren<QLabel *>())
		if (label->text() == words && !label->isHidden())
			return label;
	return nullptr;
}

bool theDock()
{
	Tally tally;
	StandInGitHub github;
	QTemporaryDir folder;
	const Layout layout{Layout::Kind::ObsFolder, folder.path()};
	const QByteArray zip = pluginZip("fresh", "MZ fresh");
	github.answers["/fresh.zip"] = {200, zip, {}};
	github.answers["/plugins.json"] = {200,
					   listOf({listed("fresh", "1.0.0", github.url("fresh.zip"), sha(zip)),
						   listed("first", "1.2.0", github.url("fresh.zip"), sha(zip))}),
					   {}};
	// "first" runs and is registered with the manager, "second" too but is not on the list.
	Pretend loaded;
	writeFile(layout.dll("first"), "MZ first");
	writeFile(layout.dll("second"), "MZ second");
	loaded["first"] = {Store::Loaded::State::Yes, layout.dll("first"), "1.0.0"};
	loaded["second"] = {Store::Loaded::State::Yes, layout.dll("second"), "1.0.0"};

	Heard first, second;
	const sarxina_plugin_info one = testPlugin(first, "first", "Test plugin");
	sarxina_plugin_info two = testPlugin(second, "second", "Another test plugin");
	two.get_properties = nullptr;
	Manager manager(Manager::Use::SelfTest, kNoChat, setupFor(github, layout, &loaded, {}));
	ManagerDock dock(manager); // made first, as in OBS, where plugins register once the dock is there
	dock.setAttribute(Qt::WA_DontShowOnScreen);
	dock.show();
	tally.expect(dropdown(dock) && dropdown(dock)->parentWidget()->isEnabled() && dockSays(dock, text("NoneInstalled")),
		     "nothing yet: the dropdown, and the list says it is empty");
	manager.add(&one);
	manager.add(&two);
	tally.expect(first.enabled && second.enabled && shown(dock, "Test plugin") && shown(dock, "Another test plugin") &&
			     hasGear(dock, "Test plugin") && !hasGear(dock, "Another test plugin") &&
			     !dockSays(dock, text("NoneInstalled")),
		     "installed plugins run, in the list, a gear for the one with settings");
	tally.expect(button(dock, "first") == text("Uninstall") && button(dock, "second") == text("Uninstall"),
		     "their buttons say Uninstall");

	manager.start();
	tally.expect(waitFor([&] { return manager.store().list() == Store::List::Ready; }), "the list fetched");
	tally.expect(button(dock, "fresh") == text("Install") && note(dock, "fresh").isEmpty() && !shown(dock, "FRESH"),
		     "a plugin on the list that is not installed: Install, not in the list");
	tally.expect(button(dock, "first", "update") == text("Update").arg("1.2.0") &&
			     button(dock, "second", "update") == "(hidden)" && button(dock, "fresh", "update") == "(hidden)",
		     "Update only for the one with a newer version");

	press(dock, "fresh");
	tally.expect(button(dock, "fresh") == text("Downloading"), "pressing Install: downloading");
	waitFor([&] { return !manager.store().state("fresh").busy; });
	tally.expect(button(dock, "fresh") == text("Uninstall") && note(dock, "fresh") == text("Restart") &&
			     dockSays(dock, text("Restart")) && !shown(dock, "FRESH"),
		     "installed: Uninstall, and restart OBS to finish");
	press(dock, "fresh");
	tally.expect(button(dock, "fresh") == text("Install") && note(dock, "fresh").isEmpty() && !dockSays(dock, text("Restart")),
		     "uninstalled again before the restart: Install, nothing to finish");

	press(dock, "first");
	tally.expect(button(dock, "first") == text("Install") && note(dock, "first") == text("Restart") && !first.enabled &&
			     !shown(dock, "Test plugin") && dockSays(dock, text("Restart")),
		     "uninstalling a running plugin: switched off, out of the list, Install, restart OBS to finish");
	press(dock, "first");
	tally.expect(button(dock, "first") == text("Uninstall") && note(dock, "first").isEmpty() && first.enabled &&
			     shown(dock, "Test plugin") && hasGear(dock, "Test plugin") && !dockSays(dock, text("Restart")),
		     "Install again: back on and in the list, nothing to finish");

	// The order in the dropdown: the list's, then the others
	QStringList order;
	for (QAction *action : dropdown(dock)->actions())
		if (auto *widget = qobject_cast<QWidgetAction *>(action); widget && widget->defaultWidget() &&
									   widget->defaultWidget()->objectName().startsWith("plugin:"))
			order << widget->defaultWidget()->objectName().mid(7);
	tally.expect(order == QStringList({"fresh", "first", "second"}), "the dropdown in the list's order");
	return tally.report("the dock", "steps as expected");
}

void saveDropdown(ManagerDock &dock, const QString &file)
{
	QMenu *menu = dropdown(dock);
	if (!menu)
		return;
	menu->setAttribute(Qt::WA_DontShowOnScreen);
	menu->popup(QPoint(0, 0));
	waitFor([] { return false; }, 100);
	test::report(menu->grab().save(file, "PNG"), "a picture of the dropdown in %s", file.toUtf8().constData());
	menu->hide();
}

// ---- 10. Twitch's API, over a stand-in for Twitch -------------------------------------------------------------------

// An http server on this computer standing in for id.twitch.tv and api.twitch.tv. It keeps each request whole and
// answers only when the test says what to answer, oldest first, so that the test sees every step.
class StandInTwitch : public QObject {
public:
	struct Request {
		QByteArray method, path, body;
		std::map<QByteArray, QByteArray> headers; // names in lower case

		QByteArray header(const char *name) const
		{
			const auto found = headers.find(name);
			return found == headers.end() ? QByteArray() : found->second;
		}
		// A field of the form it posted, decoded.
		QString field(const char *name) const
		{
			return QUrlQuery(QString::fromUtf8(body)).queryItemValue(QString::fromLatin1(name), QUrl::FullyDecoded);
		}
		bool hasField(const char *name) const
		{
			return QUrlQuery(QString::fromUtf8(body)).hasQueryItem(QString::fromLatin1(name));
		}
	};

	StandInTwitch()
	{
		server_.listen(QHostAddress::LocalHost, 0);
		connect(&server_, &QTcpServer::newConnection, this, [this] {
			while (QTcpSocket *socket = server_.nextPendingConnection()) {
				auto buffer = std::make_shared<QByteArray>();
				connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
					*buffer += socket->readAll();
					const qsizetype end = buffer->indexOf("\r\n\r\n");
					if (end < 0)
						return;
					Request request;
					const QList<QByteArray> lines = buffer->left(end).split('\n');
					const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
					request.method = first.value(0);
					request.path = first.value(1);
					for (qsizetype i = 1; i < lines.size(); i++)
						if (const qsizetype colon = lines[i].indexOf(':'); colon > 0)
							request.headers[lines[i].left(colon).trimmed().toLower()] = lines[i].mid(colon + 1).trimmed();
					const qsizetype length = request.header("content-length").toLongLong();
					if (buffer->size() < end + 4 + length)
						return;
					request.body = buffer->mid(end + 4, length);
					buffer->clear();
					got.push_back(request);
					received++;
					unanswered_.push_back(socket);
				});
			}
		});
	}

	TwitchHosts hosts() const
	{
		return *TwitchHosts::testServer(QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()));
	}

	// Waits for the next request.
	bool next(Request &request, int ms = 5000)
	{
		if (!waitFor([this] { return !got.empty(); }, ms))
			return false;
		request = got.front();
		got.pop_front();
		return true;
	}
	// Answers the oldest request not answered yet.
	void reply(int status, const QByteArray &body)
	{
		if (QTcpSocket *socket = oldest()) {
			socket->write("HTTP/1.1 " + QByteArray::number(status) +
				      " X\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
				      "\r\nConnection: close\r\n\r\n" + body);
			socket->disconnectFromHost();
		}
	}
	// Closes it without an answer, as when Twitch cannot be reached.
	void drop()
	{
		if (QTcpSocket *socket = oldest())
			socket->abort();
	}

	std::deque<Request> got;
	int received = 0;

private:
	QTcpSocket *oldest()
	{
		while (!unanswered_.empty()) {
			QPointer<QTcpSocket> socket = unanswered_.front();
			unanswered_.pop_front();
			if (socket)
				return socket;
		}
		return nullptr;
	}

	QTcpServer server_;
	std::deque<QPointer<QTcpSocket>> unanswered_;
};

// Twitch's answers, shaped as its documentation shows them (dev.twitch.tv/docs/authentication and
// dev.twitch.tv/docs/api/reference). Twitch documents no answer for a streamer saying no or for asking too often in
// the device code flow; those two have the OAuth device flow's own words (RFC 8628) in Twitch's shape.
const char *const kClientId = "selftest0client0id0abcdefghij0";
const QByteArray kDevice = R"({"device_code":"ike3GM8QIdYZs43KdrWPIO36LofILoCyFEzjlQ91","expires_in":1800,"interval":5,)"
			   R"("user_code":"ABCDEFGH","verification_uri":)"
			   R"("https://www.twitch.tv/activate?public=true&device-code=ABCDEFGH"})";
const QByteArray kPending = R"({"status":400,"message":"authorization_pending"})";
const QByteArray kSlowDown = R"({"status":400,"message":"slow_down"})";
const QByteArray kDenied = R"({"status":400,"message":"access_denied"})";
const QByteArray kExpired = R"({"status":400,"message":"invalid device code"})";
const QByteArray kToken = R"({"access_token":"rfx2uswqe8l4g1mkagrvg5tv0ks3","expires_in":14124,)"
			  R"("refresh_token":"5b93chm6hdve3mycz05zfzatkfdenfspp1h1ar2xxdalen01",)"
			  R"("scope":["channel:manage:redemptions","channel:read:redemptions","moderator:read:chatters"],"token_type":"bearer"})";
const QByteArray kValidate = R"({"client_id":"selftest0client0id0abcdefghij0","login":"sarxina",)"
			     R"("scopes":["channel:manage:redemptions","channel:read:redemptions","moderator:read:chatters"],"user_id":"141981764",)"
			     R"("expires_in":14124})";
const QByteArray kValidateOtherScopes = R"({"client_id":"selftest0client0id0abcdefghij0","login":"sarxina",)"
					R"("scopes":["channel:read:subscriptions"],"user_id":"141981764","expires_in":5520838})";
const QByteArray kUnauthorized = R"({"error":"Unauthorized","status":401,"message":"Invalid OAuth token"})";
const QByteArray kRefreshed = R"({"access_token":"1ssjqsqfy6bads1ws7m03gras79zfr","expires_in":15583,)"
			      R"("refresh_token":"eyJfMzUtNDU0OC4MWYwLTQ5MDY5ODY4NGNlMSJ9%asdfasdf=",)"
			      R"("scope":["channel:manage:redemptions","channel:read:redemptions","moderator:read:chatters"],"token_type":"bearer"})";
const QByteArray kRefreshedAgain = R"({"access_token":"2gbdx6oar67tqtcmt49t3wpcgycthx","expires_in":14400,)"
				   R"("refresh_token":"8tv8hqqo1ctvbbqepm0fmb1a1kb6b5xfrm6ma7y35ntcgxa2ce",)"
				   R"("scope":["channel:manage:redemptions","channel:read:redemptions","moderator:read:chatters"],"token_type":"bearer"})";
const QByteArray kBadRefresh = R"({"error":"Bad Request","status":400,"message":"Invalid refresh token"})";
const QByteArray kRewards =
	R"({"data":[{"broadcaster_name":"sarxina","broadcaster_login":"sarxina","broadcaster_id":"141981764",)"
	R"("id":"92af127c-7326-4483-a52b-b0da0be61c01","image":null,"background_color":"#00E5CB","is_enabled":true,)"
	R"("cost":50000,"title":"game analysis","prompt":"","is_user_input_required":false,)"
	R"("max_per_stream_setting":{"is_enabled":false,"max_per_stream":0},)"
	R"("max_per_user_per_stream_setting":{"is_enabled":false,"max_per_user_per_stream":0},)"
	R"("global_cooldown_setting":{"is_enabled":false,"global_cooldown_seconds":0},"is_paused":false,"is_in_stock":true,)"
	R"("default_image":{"url_1x":"https://static-cdn.jtvnw.net/custom-reward-images/default-1.png",)"
	R"("url_2x":"https://static-cdn.jtvnw.net/custom-reward-images/default-2.png",)"
	R"("url_4x":"https://static-cdn.jtvnw.net/custom-reward-images/default-4.png"},)"
	R"("should_redemptions_skip_request_queue":false,"redemptions_redeemed_current_stream":null,)"
	R"("cooldown_expires_at":null},)"
	R"({"broadcaster_name":"sarxina","broadcaster_login":"sarxina","broadcaster_id":"141981764",)"
	R"("id":"0c7c2a4e-6f1d-4a5b-9a43-2f8a1d6e5b77","image":null,"background_color":"#9147FF","is_enabled":true,)"
	R"("cost":100,"title":"Drop a ball","prompt":"Where should it land?","is_user_input_required":true,)"
	R"("max_per_stream_setting":{"is_enabled":false,"max_per_stream":0},)"
	R"("max_per_user_per_stream_setting":{"is_enabled":false,"max_per_user_per_stream":0},)"
	R"("global_cooldown_setting":{"is_enabled":true,"global_cooldown_seconds":30},"is_paused":false,"is_in_stock":true,)"
	R"("default_image":{"url_1x":"https://static-cdn.jtvnw.net/custom-reward-images/default-1.png",)"
	R"("url_2x":"https://static-cdn.jtvnw.net/custom-reward-images/default-2.png",)"
	R"("url_4x":"https://static-cdn.jtvnw.net/custom-reward-images/default-4.png"},)"
	R"("should_redemptions_skip_request_queue":false,"redemptions_redeemed_current_stream":null,)"
	R"("cooldown_expires_at":null}]})";
const QByteArray kCreated =
	R"({"data":[{"broadcaster_name":"sarxina","broadcaster_login":"sarxina","broadcaster_id":"141981764",)"
	R"("id":"afaa7e34-6b17-49f0-a19a-d1e76eaaf673","image":null,"background_color":"#9147FF","is_enabled":true,)"
	R"("cost":100,"title":"Drop a ball","prompt":"","is_user_input_required":false,)"
	R"("max_per_stream_setting":{"is_enabled":false,"max_per_stream":0},)"
	R"("max_per_user_per_stream_setting":{"is_enabled":false,"max_per_user_per_stream":0},)"
	R"("global_cooldown_setting":{"is_enabled":false,"global_cooldown_seconds":0},"is_paused":false,"is_in_stock":true,)"
	R"("default_image":{"url_1x":"https://static-cdn.jtvnw.net/custom-reward-images/default-1.png",)"
	R"("url_2x":"https://static-cdn.jtvnw.net/custom-reward-images/default-2.png",)"
	R"("url_4x":"https://static-cdn.jtvnw.net/custom-reward-images/default-4.png"},)"
	R"("should_redemptions_skip_request_queue":false,"redemptions_redeemed_current_stream":0,)"
	R"("cooldown_expires_at":null}]})";
const QByteArray kNotAffiliate =
	R"({"error":"Forbidden","status":403,"message":"The broadcaster must have partner or affiliate status."})";
const QByteArray kRedemptions =
	R"({"data":[{"broadcaster_name":"sarxina","broadcaster_login":"sarxina","broadcaster_id":"141981764",)"
	R"("id":"17fa2df1-ad76-4804-bfa5-a40ef63efe63","user_login":"viewer","user_id":"274637212","user_name":"Viewer",)"
	R"("user_input":"","status":"UNFULFILLED","redeemed_at":"2020-07-01T18:37:32Z",)"
	R"("reward":{"id":"92af127c-7326-4483-a52b-b0da0be61c01","title":"game analysis","prompt":"","cost":50000}}],)"
	R"("pagination":{"cursor":"eyJiIjpudWxsLCJhIjp7IkN1cnNvciI6Ik1UZG1ZVEprWmpFdFlXUTNOaTAwT0RBMExXSm1aVE10WVRRd1pXWT)"
	R"(JNMlZtWlRZelgxOHlNREl3TFRBM0xUQXhWREU0T2pNM09qTXlMakl6TXpFeU56RTFOMW89In19"}})";
const QByteArray kFulfilled =
	R"({"data":[{"broadcaster_name":"sarxina","broadcaster_login":"sarxina","broadcaster_id":"141981764",)"
	R"("id":"17fa2df1-ad76-4804-bfa5-a40ef63efe63","user_login":"viewer","user_id":"274637212","user_name":"Viewer",)"
	R"("user_input":"","status":"FULFILLED","redeemed_at":"2020-07-01T18:37:32Z",)"
	R"("reward":{"id":"92af127c-7326-4483-a52b-b0da0be61c01","title":"game analysis","prompt":"","cost":50000}}]})";
const QByteArray kScopes = "channel:read:redemptions channel:manage:redemptions moderator:read:chatters";

// What a plugin's `done` heard.
struct TwitchAnswer {
	int calls = 0;
	int status = -1;
	QByteArray body;
	bool onUiThread = true;

	QString message() const { return QJsonDocument::fromJson(body).object().value("message").toString(); }
};

void twitchAnswered(void *param, int status, const char *body)
{
	TwitchAnswer &answer = *static_cast<TwitchAnswer *>(param);
	answer.calls++;
	answer.status = status;
	answer.body = body;
	answer.onUiThread = answer.onUiThread && QThread::currentThread() == QCoreApplication::instance()->thread();
}

TwitchApi::Setup twitchSetup(const StandInTwitch &twitch, const char *clientId, const QString &remembered,
			     std::vector<QUrl> *opened, int secondMs = 40)
{
	TwitchApi::Setup setup;
	setup.hosts = twitch.hosts();
	setup.clientId = QString::fromLatin1(clientId);
	setup.remembered = remembered;
	if (opened)
		setup.open = [opened](const QUrl &url) { opened->push_back(url); };
	setup.secondMs = secondMs; // Twitch's 5 seconds between asking are 200 ms here
	return setup;
}

QJsonObject readJson(const QString &path)
{
	return QJsonDocument::fromJson(readFile(path)).object();
}

bool twitchApi()
{
	Tally tally;
	StandInTwitch twitch;
	QTemporaryDir folder;
	const QString remembered = folder.filePath("config/twitch.json");
	std::vector<QUrl> opened;
	StandInTwitch::Request r;

	// Every request a plugin sends, and what its `done` heard. The strings it gives last only for the call.
	std::deque<TwitchAnswer> answers;
	bool neverInside = true;
	const auto ask = [&](Manager &manager, const char *method, const char *path,
			     const char *body = nullptr) -> TwitchAnswer & {
		answers.emplace_back();
		TwitchAnswer &answer = answers.back();
		std::string m = method, p = path, b = body ? body : "";
		manager.twitchRequest(sarxina_twitch_request{m.c_str(), p.c_str(), body ? b.c_str() : nullptr, twitchAnswered, &answer});
		m.assign(m.size(), '?');
		p.assign(p.size(), '?');
		b.assign(b.size(), '?');
		neverInside = neverInside && answer.calls == 0;
		return answer;
	};
	const auto answered = [](TwitchAnswer &answer) { return waitFor([&answer] { return answer.calls > 0; }); };
	const auto state = [](Manager &manager) { return manager.twitchApi().state(); };
	// Connect pressed, the code entered, the tokens given, and the token checked with this answer.
	const auto connectAll = [&](Manager &manager, const QByteArray &validated) {
		manager.twitchApi().connectTwitch();
		bool ok = twitch.next(r) && r.path == "/oauth2/device";
		twitch.reply(200, kDevice);
		ok = ok && twitch.next(r) && r.path == "/oauth2/token";
		twitch.reply(200, kToken);
		ok = ok && twitch.next(r) && r.path == "/oauth2/validate";
		twitch.reply(200, validated);
		return ok && waitFor([&] { return state(manager) != TwitchApi::State::Connecting; });
	};

	// Where requests may go: Twitch's own two hosts over HTTPS, or a test server on 127.0.0.1
	const TwitchHosts real = TwitchHosts::twitch();
	tally.expect(real.id == "https://id.twitch.tv" && real.api == "https://api.twitch.tv/helix" && !real.local,
		     "Twitch: id.twitch.tv and api.twitch.tv, over HTTPS");
	const auto local = TwitchHosts::testServer("http://127.0.0.1:8790");
	tally.expect(local && local->local && local->id == "http://127.0.0.1:8790" && local->api == "http://127.0.0.1:8790/helix",
		     "a test server on 127.0.0.1");
	for (const char *other : {"http://localhost:8790", "https://127.0.0.1:8790", "http://192.168.1.5:8790", "http://127.0.0.1",
				  "http://127.0.0.1:1/x", "http://user@127.0.0.1:1"})
		tally.expect(!TwitchHosts::testServer(other), std::string("not a test server: ") + other);
	tally.expect(TwitchHttp::allowed(real, "https://api.twitch.tv/helix/channel_points/custom_rewards?broadcaster_id=1") &&
			     TwitchHttp::allowed(real, "https://id.twitch.tv/oauth2/validate"),
		     "Twitch's addresses allowed");
	for (const char *other : {"http://api.twitch.tv/helix/x", "https://api.twitch.tv.evil.example/helix/x",
				  "https://evil.example/https://api.twitch.tv/helix/", "https://api.twitch.tv:8443/helix/x",
				  "https://api.twitch.tv/kraken/x", "https://user@id.twitch.tv/oauth2/token"})
		tally.expect(!TwitchHttp::allowed(real, other), std::string("not Twitch: ") + other);

	// A build without a Twitch app id
	{
		Manager manager(Manager::Use::SelfTest, kNoChat, Store::Setup(),
				twitchSetup(twitch, "", remembered, &opened));
		tally.expect(state(manager) == TwitchApi::State::NoApp, "no app id: it says it cannot connect");
		TwitchAnswer &answer = ask(manager, "GET", "/channel_points/custom_rewards");
		tally.expect(answered(answer) && answer.status == 0 && answer.message() == text("Api.NoApp"),
			     "no app id: a request answered with status 0 and why");
		manager.twitchApi().connectTwitch();
		tally.expect(state(manager) == TwitchApi::State::NoApp && !twitch.next(r, 200), "no app id: Connect does nothing");
	}

	auto manager = std::make_unique<Manager>(Manager::Use::SelfTest, kNoChat, Store::Setup(),
						 twitchSetup(twitch, kClientId, remembered, &opened));
	TwitchApi &api = manager->twitchApi();
	tally.expect(api.state() == TwitchApi::State::Off, "not connected at first");
	TwitchAnswer &first = ask(*manager, "GET", "/channel_points/custom_rewards");
	tally.expect(answered(first) && first.status == 0 && first.message() == text("Api.NotConnected") && twitch.received == 0,
		     "not connected: status 0 and why, and nothing sent");

	// Connect: a code, the page to enter it on, asking until it is entered, slowing down when Twitch says so
	api.connectTwitch();
	tally.expect(api.state() == TwitchApi::State::Connecting && twitch.next(r) && r.method == "POST" &&
			     r.path == "/oauth2/device" &&
			     r.header("content-type") == "application/x-www-form-urlencoded" &&
			     r.field("client_id") == kClientId && r.field("scopes") == kScopes && !r.hasField("client_secret"),
		     "Connect asks Twitch for a code with the app id and both scopes, and no secret");
	twitch.reply(200, kDevice);
	tally.expect(waitFor([&] { return api.state() == TwitchApi::State::Waiting; }) && api.userCode() == "ABCDEFGH" &&
			     opened.size() == 1 &&
			     opened[0] == QUrl("https://www.twitch.tv/activate?public=true&device-code=ABCDEFGH"),
		     "waiting for the code, with twitch.tv/activate opened");
	QElapsedTimer clock;
	clock.start();
	tally.expect(twitch.next(r) && r.method == "POST" && r.path == "/oauth2/token" && r.field("client_id") == kClientId &&
			     r.field("device_code") == "ike3GM8QIdYZs43KdrWPIO36LofILoCyFEzjlQ91" &&
			     r.field("grant_type") == "urn:ietf:params:oauth:grant-type:device_code" &&
			     r.field("scopes") == kScopes,
		     "asking whether the code was entered");
	twitch.reply(400, kPending);
	tally.expect(twitch.next(r) && r.path == "/oauth2/token" && api.state() == TwitchApi::State::Waiting,
		     "not yet: asking again");
	twitch.reply(400, kSlowDown);
	const qint64 slowed = clock.elapsed();
	tally.expect(twitch.next(r) && r.path == "/oauth2/token" && clock.elapsed() - slowed >= 380,
		     "slow_down: asking 5 seconds less often");
	twitch.reply(200, kToken);
	tally.expect(twitch.next(r) && r.method == "GET" && r.path == "/oauth2/validate" &&
			     r.header("authorization") == "OAuth rfx2uswqe8l4g1mkagrvg5tv0ks3",
		     "the new token checked with Twitch");
	twitch.reply(200, kValidate);
	tally.expect(waitFor([&] { return api.state() == TwitchApi::State::Ready; }) && api.login() == "sarxina",
		     "connected as sarxina");
	QJsonObject kept = readJson(remembered);
	tally.expect(kept.value("access_token").toString() == "rfx2uswqe8l4g1mkagrvg5tv0ks3" &&
			     kept.value("refresh_token").toString() == "5b93chm6hdve3mycz05zfzatkfdenfspp1h1ar2xxdalen01" &&
			     kept.value("login").toString() == "sarxina" && kept.value("user_id").toString() == "141981764",
		     "the tokens kept in twitch.json");

	// Plugins' requests: the broadcaster, the Client-Id and the token added, Twitch's answer passed on as it came
	TwitchAnswer &rewards = ask(*manager, "GET", "/channel_points/custom_rewards");
	tally.expect(twitch.next(r) && r.method == "GET" &&
			     r.path == "/helix/channel_points/custom_rewards?broadcaster_id=141981764" &&
			     r.header("client-id") == kClientId &&
			     r.header("authorization") == "Bearer rfx2uswqe8l4g1mkagrvg5tv0ks3" && r.body.isEmpty(),
		     "a GET with broadcaster_id, Client-Id and the token added");
	twitch.reply(200, kRewards);
	const QJsonArray listedRewards =
		answered(rewards) ? QJsonDocument::fromJson(rewards.body).object().value("data").toArray() : QJsonArray();
	tally.expect(rewards.status == 200 && rewards.body == kRewards && listedRewards.size() == 2 &&
			     listedRewards[1].toObject().value("is_user_input_required").toBool(),
		     "the rewards reaching the plugin as Twitch sent them");

	TwitchAnswer &manageable = ask(*manager, "GET", "/channel_points/custom_rewards?only_manageable_rewards=true");
	tally.expect(twitch.next(r) &&
			     r.path == "/helix/channel_points/custom_rewards?only_manageable_rewards=true&broadcaster_id=141981764",
		     "broadcaster_id added to a query the path has");
	twitch.reply(200, R"({"data":[]})");
	answered(manageable);
	TwitchAnswer &otherChannel = ask(*manager, "GET", "/channel_points/custom_rewards?broadcaster_id=999&first=1");
	tally.expect(twitch.next(r) && r.path == "/helix/channel_points/custom_rewards?first=1&broadcaster_id=141981764",
		     "a broadcaster_id in the path replaced by the connected one");
	twitch.reply(200, R"({"data":[]})");
	answered(otherChannel);

	const char *newReward = R"({"title":"Drop a ball","cost":100,"is_user_input_required":false})";
	TwitchAnswer &created = ask(*manager, "POST", "/channel_points/custom_rewards", newReward);
	tally.expect(twitch.next(r) && r.method == "POST" &&
			     r.path == "/helix/channel_points/custom_rewards?broadcaster_id=141981764" &&
			     r.header("content-type") == "application/json" && r.body == newReward,
		     "a POST with its JSON body");
	twitch.reply(200, kCreated);
	tally.expect(answered(created) && created.status == 200 &&
			     QJsonDocument::fromJson(created.body).object().value("data")[0].toObject().value("id").toString() ==
				     "afaa7e34-6b17-49f0-a19a-d1e76eaaf673",
		     "a new reward: its id reaching the plugin");
	TwitchAnswer &refused = ask(*manager, "POST", "/channel_points/custom_rewards", newReward);
	twitch.next(r);
	twitch.reply(403, kNotAffiliate);
	tally.expect(answered(refused) && refused.status == 403 && refused.body == kNotAffiliate,
		     "not an affiliate: Twitch's 403 passed on unchanged");

	TwitchAnswer &waiting = ask(*manager, "GET",
				    "/channel_points/custom_rewards/redemptions?reward_id=92af127c-7326-4483-a52b-b0da0be61c01"
				    "&status=UNFULFILLED");
	tally.expect(twitch.next(r) && r.method == "GET" &&
			     r.path == "/helix/channel_points/custom_rewards/redemptions?reward_id=92af127c-7326-4483-a52b-"
				       "b0da0be61c01&status=UNFULFILLED&broadcaster_id=141981764",
		     "the redemptions waiting");
	twitch.reply(200, kRedemptions);
	tally.expect(answered(waiting) && waiting.status == 200 && waiting.body == kRedemptions, "and their answer");
	TwitchAnswer &fulfilled = ask(*manager, "PATCH",
				      "/channel_points/custom_rewards/redemptions?id=17fa2df1-ad76-4804-bfa5-a40ef63efe63"
				      "&reward_id=92af127c-7326-4483-a52b-b0da0be61c01",
				      R"({"status":"FULFILLED"})");
	tally.expect(twitch.next(r) && r.method == "PATCH" &&
			     r.path == "/helix/channel_points/custom_rewards/redemptions?id=17fa2df1-ad76-4804-bfa5-"
				       "a40ef63efe63&reward_id=92af127c-7326-4483-a52b-b0da0be61c01&broadcaster_id=141981764" &&
			     r.body == R"({"status":"FULFILLED"})" && r.header("content-type") == "application/json",
		     "a redemption marked FULFILLED with PATCH");
	twitch.reply(200, kFulfilled);
	tally.expect(answered(fulfilled) && fulfilled.status == 200 && fulfilled.body == kFulfilled, "and its answer");

	const int sent = twitch.received;
	TwitchAnswer &badMethod = ask(*manager, "PUT", "/channel_points/custom_rewards");
	TwitchAnswer &badPath = ask(*manager, "GET", "/../users");
	tally.expect(answered(badMethod) && answered(badPath) && badMethod.status == 0 && badPath.status == 0 &&
			     badPath.message() == text("Api.NotARequest") && twitch.received == sent,
		     "a method or a path Twitch's API does not take: not sent");

	// The token ran out: refreshed once, the new refresh token kept, the request sent again with the new token
	TwitchAnswer &expired = ask(*manager, "GET", "/channel_points/custom_rewards");
	twitch.next(r);
	twitch.reply(401, kUnauthorized);
	tally.expect(twitch.next(r) && r.method == "POST" && r.path == "/oauth2/token" &&
			     r.field("grant_type") == "refresh_token" &&
			     r.field("refresh_token") == "5b93chm6hdve3mycz05zfzatkfdenfspp1h1ar2xxdalen01" &&
			     r.field("client_id") == kClientId && !r.hasField("client_secret"),
		     "a 401: the token refreshed, as a public app, with no secret");
	twitch.reply(200, kRefreshed);
	tally.expect(twitch.next(r) && r.path == "/helix/channel_points/custom_rewards?broadcaster_id=141981764" &&
			     r.header("authorization") == "Bearer 1ssjqsqfy6bads1ws7m03gras79zfr",
		     "and the request sent again with the new token");
	twitch.reply(200, kRewards);
	kept = readJson(remembered);
	tally.expect(answered(expired) && expired.status == 200 && expired.body == kRewards &&
			     kept.value("access_token").toString() == "1ssjqsqfy6bads1ws7m03gras79zfr" &&
			     kept.value("refresh_token").toString() == "eyJfMzUtNDU0OC4MWYwLTQ5MDY5ODY4NGNlMSJ9%asdfasdf=",
		     "its answer reaching the plugin, and the new refresh token kept");

	// Two requests refused at once: one refresh for both
	TwitchAnswer &one = ask(*manager, "GET", "/channel_points/custom_rewards");
	TwitchAnswer &two = ask(*manager, "GET", "/channel_points/custom_rewards/redemptions?reward_id=1&status=UNFULFILLED");
	twitch.next(r);
	twitch.reply(401, kUnauthorized);
	twitch.next(r);
	twitch.reply(401, kUnauthorized);
	tally.expect(twitch.next(r) && r.path == "/oauth2/token" &&
			     r.field("refresh_token") == "eyJfMzUtNDU0OC4MWYwLTQ5MDY5ODY4NGNlMSJ9%asdfasdf=",
		     "two refused at once: one refresh, with the refresh token as Twitch gave it");
	twitch.reply(200, kRefreshedAgain);
	// Requests go to Twitch one at a time: the second once the first is answered.
	bool newToken = twitch.next(r) && r.header("authorization") == "Bearer 2gbdx6oar67tqtcmt49t3wpcgycthx";
	twitch.reply(200, kRewards);
	newToken = newToken && twitch.next(r) && r.header("authorization") == "Bearer 2gbdx6oar67tqtcmt49t3wpcgycthx";
	twitch.reply(200, kRedemptions);
	tally.expect(newToken, "both sent again with the new token");
	tally.expect(answered(one) && answered(two) && one.status == 200 && two.status == 200 && !twitch.next(r, 200),
		     "both answered, nothing more sent");

	// The refresh refused: it needs reconnecting
	TwitchAnswer &signedOut = ask(*manager, "GET", "/channel_points/custom_rewards");
	twitch.next(r);
	twitch.reply(401, kUnauthorized);
	twitch.next(r);
	twitch.reply(400, kBadRefresh);
	const bool told = answered(signedOut);
	kept = readJson(remembered);
	tally.expect(told && signedOut.status == 0 && signedOut.message() == text("Api.Reconnect") &&
			     api.state() == TwitchApi::State::Reconnect && api.why() == TwitchApi::Why::SignedOut &&
			     !kept.contains("access_token") && !kept.contains("refresh_token") &&
			     kept.value("reconnect").toString() == "signed-out",
		     "a refresh Twitch refuses: needs reconnecting, the tokens forgotten");
	TwitchAnswer &afterSignOut = ask(*manager, "GET", "/channel_points/custom_rewards");
	tally.expect(answered(afterSignOut) && afterSignOut.status == 0 &&
			     afterSignOut.message() == text("Api.Reconnect") && !twitch.next(r, 200),
		     "and a request then says to reconnect, sending nothing");

	// Reconnecting with a token that lacks the channel point scopes
	tally.expect(connectAll(*manager, kValidateOtherScopes) && api.state() == TwitchApi::State::Reconnect &&
			     api.why() == TwitchApi::Why::Scope,
		     "a token without the channel point scopes: needs reconnecting");
	TwitchAnswer &noScope = ask(*manager, "POST", "/channel_points/custom_rewards", newReward);
	tally.expect(answered(noScope) && noScope.status == 0 &&
			     noScope.message() == "Reconnect Twitch in the Sarxina OBS Plugin Manager." &&
			     !twitch.next(r, 200),
		     "and a request says to reconnect Twitch in the manager");

	// The streamer says no, the code runs out, Twitch cannot be reached
	const auto endsWith = [&](const QByteArray &answer, TwitchApi::Why why) {
		api.connectTwitch();
		bool ok = twitch.next(r) && r.path == "/oauth2/device";
		twitch.reply(200, kDevice);
		ok = ok && twitch.next(r) && r.path == "/oauth2/token";
		if (answer.isEmpty())
			twitch.drop();
		else
			twitch.reply(400, answer);
		return ok && waitFor([&] { return api.state() != TwitchApi::State::Waiting; }) &&
		       api.state() == TwitchApi::State::Off && api.why() == why;
	};
	tally.expect(endsWith(kDenied, TwitchApi::Why::Denied), "the streamer says no on Twitch: not connected, and why");
	tally.expect(endsWith(kExpired, TwitchApi::Why::Expired), "the code ran out: not connected, and why");
	api.connectTwitch();
	twitch.next(r);
	twitch.drop();
	tally.expect(waitFor([&] { return api.state() == TwitchApi::State::Off; }) &&
			     api.why() == TwitchApi::Why::Unreachable,
		     "Twitch cannot be reached: not connected, and why");

	// The next time OBS starts: connected at once, and the token checked
	tally.expect(connectAll(*manager, kValidate) && api.state() == TwitchApi::State::Ready, "connected again");
	manager.reset();
	manager = std::make_unique<Manager>(Manager::Use::SelfTest, kNoChat, Store::Setup(),
					    twitchSetup(twitch, kClientId, remembered, &opened));
	tally.expect(manager->twitchApi().state() == TwitchApi::State::Ready && manager->twitchApi().login() == "sarxina",
		     "the next start: connected as sarxina from twitch.json");
	manager->start();
	tally.expect(twitch.next(r) && r.path == "/oauth2/validate" && r.header("authorization") == "OAuth rfx2uswqe8l4g1mkagrvg5tv0ks3",
		     "the next start: the token checked when OBS has loaded");
	twitch.reply(200, kValidate);

	// OBS closing with a request on its way: its answer never reaches the plugin
	TwitchAnswer &late = ask(*manager, "GET", "/channel_points/custom_rewards");
	tally.expect(twitch.next(r), "a request on its way");
	manager->shutDown();
	twitch.reply(200, kRewards);
	TwitchAnswer &afterClose = ask(*manager, "GET", "/channel_points/custom_rewards");
	waitFor([] { return false; }, 300);
	tally.expect(late.calls == 0 && afterClose.calls == 0, "OBS closing: no answer reaches a plugin after that");

	// Disconnect: Twitch told to forget the token, and twitch.json too
	manager = std::make_unique<Manager>(Manager::Use::SelfTest, kNoChat, Store::Setup(),
					    twitchSetup(twitch, kClientId, remembered, &opened));
	manager->twitchApi().disconnectTwitch();
	tally.expect(twitch.next(r) && r.path == "/oauth2/revoke" && r.field("client_id") == kClientId &&
			     r.field("token") == "rfx2uswqe8l4g1mkagrvg5tv0ks3",
		     "Disconnect: Twitch told to forget the token");
	twitch.reply(200, {});
	tally.expect(manager->twitchApi().state() == TwitchApi::State::Off && !readJson(remembered).contains("access_token"),
		     "Disconnect: not connected, the tokens forgotten");
	manager.reset();

	// Twitch wants the token checked every hour. Here a second is 1 ms, and an hour 3.6 seconds.
	{
		Manager hourly(Manager::Use::SelfTest, kNoChat, Store::Setup(),
			       twitchSetup(twitch, kClientId, folder.filePath("hourly.json"), &opened, 1));
		connectAll(hourly, kValidate);
		tally.expect(hourly.twitchApi().state() == TwitchApi::State::Ready && twitch.next(r, 8000) &&
				     r.path == "/oauth2/validate",
			     "the token checked again an hour later");
		twitch.reply(200, kValidate);
		hourly.shutDown();
	}

	waitFor([] { return false; }, 100);
	bool once = neverInside;
	for (const TwitchAnswer &answer : answers)
		once = once && answer.onUiThread && (answer.calls == 1 || &answer == &late || &answer == &afterClose);
	tally.expect(once, "every answer later than its request, exactly once, on the UI thread");
	return tally.report("Twitch's API", "steps as expected");
}

// ---- 11. the dock's line for Twitch ---------------------------------------------------------------------------------

bool twitchDock()
{
	Tally tally;
	StandInTwitch twitch;
	StandIn chat;
	std::vector<QUrl> opened;
	StandInTwitch::Request r;
	const auto says = [](ManagerDock &dock) {
		QLabel *label = dock.findChild<QLabel *>("twitch-state");
		return label ? label->text() : QStringLiteral("(none)");
	};
	const auto button = [](ManagerDock &dock) {
		QPushButton *button = dock.findChild<QPushButton *>("twitch-button");
		return !button ? QStringLiteral("(none)") : button->isHidden() ? QStringLiteral("(hidden)") : button->text();
	};
	const auto press = [](ManagerDock &dock) {
		if (QPushButton *button = dock.findChild<QPushButton *>("twitch-button"))
			button->click();
	};
	const auto note = [](ManagerDock &dock) {
		QLabel *label = dock.findChild<QLabel *>("twitch-chat");
		return !label || label->isHidden() ? QString() : label->text();
	};

	{
		Manager manager(Manager::Use::SelfTest, chat.url(), Store::Setup(), TwitchApi::Setup());
		ManagerDock dock(manager);
		tally.expect(says(dock) == text("Twitch.NoApp") && button(dock) == "(hidden)" && note(dock).isEmpty(),
			     "a build without a Twitch app id: says so, no button, no chat");
	}

	Manager manager(Manager::Use::SelfTest, chat.url(), Store::Setup(), twitchSetup(twitch, kClientId, {}, &opened));
	ManagerDock dock(manager);
	dock.setAttribute(Qt::WA_DontShowOnScreen);
	dock.show();
	TwitchApi &api = manager.twitchApi();
	tally.expect(says(dock) == text("Off") && button(dock) == text("Connect"), "not connected: Connect");

	press(dock);
	tally.expect(says(dock) == text("Twitch.Connecting") && button(dock) == "(hidden)", "Connect pressed: connecting");
	twitch.next(r);
	twitch.reply(200, kDevice);
	waitFor([&] { return api.state() == TwitchApi::State::Waiting; });
	QLabel *state = dock.findChild<QLabel *>("twitch-state");
	tally.expect(says(dock) == "Go to twitch.tv/activate and enter ABCDEFGH" && button(dock) == text("Open") && state &&
			     (state->textInteractionFlags() & Qt::TextSelectableByMouse),
		     "the code to enter, which can be copied, and Open");
	press(dock);
	tally.expect(opened.size() == 2 && opened[1] == opened[0], "Open opens twitch.tv/activate again");
	twitch.next(r);
	twitch.reply(200, kToken);
	twitch.next(r);
	twitch.reply(200, kValidate);
	waitFor([&] { return api.state() == TwitchApi::State::Ready; });
	tally.expect(says(dock) == "Connected as sarxina" && button(dock) == text("Disconnect"),
		     "Connected as sarxina, and Disconnect");

	// The chat of the account connected, with nothing more to do: no OBS account, no restart.
	QByteArray cap, nick, join;
	tally.expect(chat.next(cap) && chat.next(nick) && chat.next(join) && join == "JOIN #sarxina" &&
			     manager.twitch().channel() == "sarxina",
		     "connected: reads the chat of the account connected");
	tally.expect(note(dock) == "Connecting to #sarxina's chat...", "and says so under it");
	chat.send(":" + nick.mid(5) + "!x@x JOIN #sarxina");
	waitFor([&] { return manager.twitch().state() == TwitchChat::State::Reading; });
	tally.expect(note(dock) == "Reading #sarxina's chat", "Reading #sarxina's chat, once Twitch says it joined");

	TwitchAnswer answer;
	manager.twitchRequest({"GET", "/channel_points/custom_rewards", nullptr, twitchAnswered, &answer});
	twitch.next(r);
	twitch.reply(401, kUnauthorized);
	twitch.next(r);
	twitch.reply(400, kBadRefresh);
	waitFor([&] { return api.state() == TwitchApi::State::Reconnect; });
	tally.expect(says(dock) == "Needs reconnecting: Twitch signed it out." && button(dock) == text("Reconnect") &&
			     manager.twitch().state() == TwitchChat::State::Off && note(dock).isEmpty(),
		     "signed out: Needs reconnecting, and Reconnect, and chat stops with it");

	press(dock);
	twitch.next(r);
	twitch.reply(200, kDevice);
	twitch.next(r);
	twitch.reply(200, kToken);
	twitch.next(r);
	twitch.reply(200, kValidateOtherScopes);
	waitFor([&] { return api.state() == TwitchApi::State::Reconnect; });
	tally.expect(says(dock) == "Needs reconnecting: it isn't allowed everything the plugins need." &&
			     button(dock) == text("Reconnect"),
		     "a scope missing: Needs reconnecting, and Reconnect");

	press(dock);
	twitch.next(r);
	twitch.reply(200, kDevice);
	twitch.next(r);
	twitch.reply(400, kDenied);
	waitFor([&] { return api.state() == TwitchApi::State::Off; });
	tally.expect(says(dock) == text("Twitch.Denied") && button(dock) == text("Connect"), "not allowed on Twitch: Connect");

	press(dock);
	twitch.next(r);
	twitch.drop();
	waitFor([&] { return api.why() == TwitchApi::Why::Unreachable; });
	tally.expect(says(dock) == text("Twitch.Unreachable") && button(dock) == text("Connect"),
		     "Twitch cannot be reached: Connect");

	press(dock);
	twitch.next(r);
	twitch.reply(200, kDevice);
	twitch.next(r);
	twitch.reply(200, kToken);
	twitch.next(r);
	twitch.reply(200, kValidate);
	waitFor([&] { return api.state() == TwitchApi::State::Ready; });
	press(dock);
	tally.expect(twitch.next(r) && r.path == "/oauth2/revoke" && says(dock) == text("Off") &&
			     button(dock) == text("Connect") && manager.twitch().state() == TwitchChat::State::Off &&
			     note(dock).isEmpty(),
		     "Disconnect: not connected, Connect, and no chat");
	twitch.reply(200, {});
	return tally.report("Twitch in the dock", "steps as expected");
}

// ---- 12. buttons in a plugin's settings -----------------------------------------------------------------------------

struct Clicks {
	int properties = 0; // times the settings were asked for
	int clicked = 0;
	void *data = nullptr; // what the button's callback was handed
	bool redraw = true;   // what it answers
	PropertiesForm *form = nullptr;
	bool reloadInside = false; // the plugin's settings change while its callback runs, as with settings_changed
	std::string note = "No reward made yet.";
};

bool settingsForm()
{
	Tally tally;
	Clicks clicks;
	sarxina_plugin_info info{};
	info.api = SARXINA_OBS_PLUGIN_MANAGER_API;
	info.id = "buttons";
	info.name = "Buttons";
	info.data = &clicks;
	info.set_enabled = [](void *, bool) {};
	info.get_properties = [](void *data) {
		Clicks &clicks = *static_cast<Clicks *>(data);
		clicks.properties++;
		obs_properties_t *properties = obs_properties_create();
		obs_properties_add_text(properties, "made", clicks.note.c_str(), OBS_TEXT_INFO);
		obs_property_t *make = obs_properties_add_button2(
			properties, "make", "Create a new redeem",
			[](obs_properties_t *, obs_property_t *, void *data) {
				Clicks &clicks = *static_cast<Clicks *>(data);
				clicks.clicked++;
				clicks.data = data;
				clicks.note = "Clicked " + std::to_string(clicks.clicked) + " times.";
				if (clicks.reloadInside && clicks.form)
					clicks.form->reload();
				return clicks.redraw;
			},
			data);
		obs_property_set_long_description(make, "Makes a reward on Twitch for this.");
		return properties;
	};
	Plugin plugin{&info};

	QWidget window;
	window.setAttribute(Qt::WA_DontShowOnScreen);
	auto *form = new PropertiesForm(plugin, &window);
	clicks.form = form;
	window.show();
	const auto settle = [] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); };
	const auto theButton = [form]() -> QPushButton * {
		for (QPushButton *button : form->findChildren<QPushButton *>())
			if (button->text() == "Create a new redeem")
				return button;
		return nullptr;
	};
	const auto noted = [form](const char *words) {
		for (QLabel *label : form->findChildren<QLabel *>())
			if (label->text() == words)
				return true;
		return false;
	};

	tally.expect(theButton() && theButton()->toolTip() == "Makes a reward on Twitch for this." && clicks.properties == 1 &&
			     noted("No reward made yet."),
		     "a button drawn with its words and its tip, beside a note");
	theButton()->click();
	settle();
	tally.expect(clicks.clicked == 1 && clicks.data == &clicks, "clicking it calls the plugin with the button's data");
	tally.expect(clicks.properties == 2 && noted("Clicked 1 times.") && theButton(),
		     "it answered true: the settings asked for again and drawn anew");
	clicks.redraw = false;
	theButton()->click();
	settle();
	tally.expect(clicks.clicked == 2 && clicks.properties == 2 && noted("Clicked 1 times."),
		     "it answered false: nothing drawn again");
	clicks.reloadInside = true;
	theButton()->click();
	settle();
	tally.expect(clicks.clicked == 3 && clicks.properties == 3 && noted("Clicked 3 times."),
		     "its settings changed while it ran: drawn anew once, after it returned");
	clicks.reloadInside = false;
	clicks.note = "Changed by the plugin.";
	form->reload(); // what the dock does when a plugin calls sarxina_obs_plugin_manager_settings_changed
	settle();
	tally.expect(clicks.properties == 4 && noted("Changed by the plugin."), "a note the plugin changed, shown anew");
	return tally.report("settings", "steps as expected");
}

// ---- the picture ----------------------------------------------------------------------------------------------------

void picture(const char *folder)
{
	if (!folder || !*folder || !QDir().mkpath(QString::fromUtf8(folder)))
		return;
	StandIn twitch;
	StandInGitHub github;
	QTemporaryDir plugins;
	const Layout layout{Layout::Kind::ObsFolder, plugins.path()};
	const QByteArray zip = pluginZip("slinglive", "MZ");
	github.answers["/slinglive.zip"] = {200, zip, {}};
	QJsonArray list;
	const struct {
		const char *id, *name, *description;
	} shownPlugins[] = {
		{"first", "Test plugin", "Only here for the self-test."},
		{"slinglive", "SlingLive", "Replaces the Controls panel with buttons you press by flinging a head at them."},
		{"second", "Another test plugin", "Switched off, and with no settings."},
		{"sarxina-parametric-eq", "Sarxina Parametric EQ", "A parametric equalizer for audio sources."},
		{"sarxina-noise-gate", "Sarxina Noise Gate", "A voice noise gate."},
	};
	for (const auto &plugin : shownPlugins) {
		QJsonObject entry = listed(plugin.id, "1.0.0", github.url("slinglive.zip"), sha(zip));
		entry["name"] = plugin.name;
		entry["description"] = plugin.description;
		if (QLatin1String(plugin.id) == "first")
			entry["version"] = "1.1.0";
		list.append(entry);
	}
	github.answers["/plugins.json"] = {200, listOf(list), {}};
	Pretend loaded;
	for (const char *id : {"first", "second", "sarxina-noise-gate"}) {
		writeFile(layout.dll(id), "MZ");
		loaded[id] = {Store::Loaded::State::Yes, layout.dll(id), "1.0.0"};
	}

	Heard first, second;
	const sarxina_plugin_info one = testPlugin(first, "first", "Test plugin");
	sarxina_plugin_info two = testPlugin(second, "second", "Another test plugin");
	two.commands = nullptr;
	two.command_count = 0;
	two.get_properties = nullptr;
	two.description = "Switched off, and with no settings.";

	StandInTwitch points;
	Manager manager(Manager::Use::SelfTest, twitch.url(), setupFor(github, layout, &loaded, {}),
			twitchSetup(points, kClientId, {}, nullptr));
	ManagerDock dock(manager);
	manager.add(&one);
	manager.add(&two);
	manager.start();
	waitFor([&manager] { return manager.store().list() == Store::List::Ready; });
	manager.store().install("slinglive");
	waitFor([&manager] { return !manager.store().state("slinglive").busy; });
	manager.store().uninstall("second"); // switched off: waiting for a restart
	dock.setAttribute(Qt::WA_DontShowOnScreen);
	dock.resize(380, 520);
	dock.show();
	for (QCheckBox *title : dock.findChildren<QCheckBox *>())
		if (title->property("class").toString() == "indicator-expand" && title->text() == "Test plugin")
			title->setChecked(false); // its description, unfolded

	// Twitch: the code to enter, then connected, and the chat of the account connected
	StandInTwitch::Request request;
	manager.twitchApi().connectTwitch();
	points.next(request);
	points.reply(200, kDevice);
	waitFor([&manager] { return manager.twitchApi().state() == TwitchApi::State::Waiting; });
	const QString code = QDir(QString::fromUtf8(folder)).filePath(QStringLiteral("dock-code.png"));
	test::report(dock.grab().save(code, "PNG"), "a picture of the dock with the code to enter in %s",
		     code.toUtf8().constData());
	points.next(request);
	points.reply(200, kToken);
	points.next(request);
	points.reply(200, kValidate);
	waitFor([&manager] { return manager.twitchApi().state() == TwitchApi::State::Ready; });
	QByteArray cap, nick, join;
	twitch.next(cap), twitch.next(nick), twitch.next(join);
	twitch.send(":" + nick.mid(5) + "!x@x JOIN #sarxina");
	waitFor([&manager] { return manager.twitch().state() == TwitchChat::State::Reading; });

	const QString file = QDir(QString::fromUtf8(folder)).filePath(QStringLiteral("dock.png"));
	test::report(dock.grab().save(file, "PNG"), "a picture of the dock in %s", file.toUtf8().constData());
	saveDropdown(dock, QDir(QString::fromUtf8(folder)).filePath(QStringLiteral("dropdown.png")));
	manager.followTwitchAccount("");

	// What the gear shows: the settings the plugin describes, drawn by the manager.
	QWidget settings;
	settings.setAttribute(Qt::WA_DontShowOnScreen);
	auto *layoutOfSettings = new QVBoxLayout(&settings);
	layoutOfSettings->addWidget(new PropertiesForm(*manager.plugins()[0], &settings));
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
	const QString many = QDir(QString::fromUtf8(folder)).filePath(QStringLiteral("dock-25-plugins.png"));
	test::report(dock.grab().save(many, "PNG"), "a picture of the dock with 25 plugins in %s", many.toUtf8().constData());
	saveDropdown(dock, QDir(QString::fromUtf8(folder)).filePath(QStringLiteral("dropdown-25-plugins.png")));
}

// ---- GitHub itself, when asked ---------------------------------------------------------------------------------------

// With SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_ONLINE=1: a real download through GitHub, the way a plugin's zip comes, and
// the raw file host the list comes from. The zip is a release that is out already, checked against its SHA-256.
bool online()
{
	if (qEnvironmentVariableIntValue("SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_ONLINE") == 0)
		return true;
	Tally tally;
	Fetcher fetcher;
	const Source github = Source::github();
	struct Got {
		bool done = false, ok = false;
		QByteArray body;
		QString problem;
	};
	const auto get = [&](const QString &url, qint64 limit) {
		auto got = std::make_shared<Got>();
		fetcher.get(github, url, limit, [got](bool ok, const QByteArray &body, const QString &problem) {
			*got = {true, ok, body, problem};
		});
		waitFor([got] { return got->done; }, 60000);
		return *got;
	};
	const Got zip = get(github.downloads + "plinkolive-v1.1.0/plinkolive-1.1.0-windows-x64.zip", kLargestZip);
	tally.expect(zip.ok && sha(zip.body) == "ea503ae88612dc475706a342190533289f96fc5178ae8128699b084d227ee898",
		     "a release zip from GitHub, redirects and all: " + zip.problem.toStdString());
	std::vector<ZipFile> unpacked;
	QString why;
	tally.expect(readZip(zip.body, unpacked, why) && unpacked.size() >= 2 &&
			     unpacked[0].name == "obs-plugins/64bit/plinkolive.dll",
		     "and it unpacks: " + why.toStdString());
	const Got readme = get("https://raw.githubusercontent.com/Sarxina/sarxina-obs-plugins/main/README.md", kLargestIndex);
	tally.expect(readme.ok && readme.body.startsWith("# Sarxina OBS Plugins"),
		     "a file from the host the list comes from: " + readme.problem.toStdString());
	const Got list = get(github.index, kLargestIndex);
	test::report(true, "the list on GitHub: %s", list.ok ? "there" : list.problem.toUtf8().constData());
	return tally.report("GitHub", "steps as expected");
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	bool passed = parsing();
	passed &= checks();
	passed &= routing();
	passed &= twitchConnection();
	passed &= vtubeStudio();
	passed &= catalog();
	passed &= zips();
	passed &= installing();
	passed &= theDock();
	passed &= twitchApi();
	passed &= twitchDock();
	passed &= settingsForm();
	passed &= online();
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
		names += (names.empty() ? "" : ", ") + std::string(plugin->info->name) + (plugin->enabled ? " (on)" : " (off)");
	test::report(!names.empty(), "registered through OBS: %s", names.empty() ? "nothing" : names.c_str());

	// Once Twitch is connected: the channel's rewards, asked through OBS the way a plugin asks. Only reads.
	const TwitchApi &api = manager.twitchApi();
	if (api.state() != TwitchApi::State::Ready) {
		test::report(true, "Twitch: %s",
			     api.state() == TwitchApi::State::NoApp ? "this build has no Twitch app id" : "not connected");
	} else {
		static const sarxina_twitch_request rewards{
			"GET", "/channel_points/custom_rewards", nullptr,
			[](void *, int status, const char *body) {
				const QJsonArray data = QJsonDocument::fromJson(body).object().value("data").toArray();
				test::report(status == 200, "Twitch, asked the way a plugin asks: %d, %d rewards%s%s", status,
					     static_cast<int>(data.size()), status == 200 ? "" : ": ", status == 200 ? "" : body);
			},
			nullptr};
		test::report(sarxina_obs_plugin_manager_twitch_request(&rewards), "a Twitch request sent through OBS as %s",
			     api.login().toUtf8().constData());
	}

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

// Inside OBS, with SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PRESS=Connect:twitch: presses Connect next to Twitch in the dock
// the way a click does (unless it is connected already), says in the log what the dock's Twitch line says each time it
// changes, the code to enter included, and once it is connected, which rewards each plugin's settings offer. Only
// reads from Twitch: no reward is made.
static void connectInObs(ManagerDock &dock, Manager &manager)
{
	const auto label = [&dock](const char *name) {
		QLabel *found = dock.findChild<QLabel *>(name);
		return found && !found->isHidden() ? found->text() : QString();
	};
	QPushButton *connectButton = dock.findChild<QPushButton *>("twitch-button");
	const TwitchApi::State at = manager.twitchApi().state();
	if (connectButton && (at == TwitchApi::State::Off || at == TwitchApi::State::Reconnect)) {
		test::report(true, "in OBS: pressing %s next to Twitch", connectButton->text().toUtf8().constData());
		connectButton->click();
	}
	// Each plugin's reward dropdown, as its settings offer it.
	const auto rewards = [&manager](bool say) {
		for (const auto &plugin : manager.plugins()) {
			if (!plugin->enabled || !plugin->info->get_properties)
				continue;
			obs_properties_t *properties = plugin->info->get_properties(plugin->info->data);
			obs_property_t *reward = properties ? obs_properties_get(properties, "reward") : nullptr;
			if (reward && say) {
				QStringList names;
				const size_t count = obs_property_list_item_count(reward);
				for (size_t i = 0; i < count; i++)
					names << QString::fromUtf8(obs_property_list_item_name(reward, i));
				obs_property_t *help = obs_properties_get(properties, "reward_help");
				test::report(count > 1, "in OBS, %s's reward dropdown: %d lines: %s (under it: %s)",
					     plugin->info->name, static_cast<int>(count), names.join(" | ").toUtf8().constData(),
					     help ? obs_property_description(help) : "nothing");
			}
			obs_properties_destroy(properties);
		}
	};
	auto *timer = new QTimer(&dock);
	auto last = std::make_shared<QString>();
	auto ready = std::make_shared<int>(0); // ticks since it was connected
	QObject::connect(timer, &QTimer::timeout, &dock, [=, &manager] {
		const QString now = label("twitch-state") + " | " + label("twitch-chat");
		if (now != *last) {
			*last = now;
			test::report(true, "in OBS, the dock's Twitch line: %s", now.toUtf8().constData());
		}
		const TwitchApi::State state = manager.twitchApi().state();
		if (state == TwitchApi::State::Off || state == TwitchApi::State::NoApp || state == TwitchApi::State::Reconnect) {
			test::report(false, "in OBS: Twitch did not connect");
			timer->stop();
			timer->deleteLater();
			return;
		}
		if (state != TwitchApi::State::Ready)
			return;
		const int tick = ++*ready;
		if (tick == 1) {
			test::report(true, "in OBS: Twitch connected as %s", manager.twitchApi().login().toUtf8().constData());
			rewards(false); // asks Twitch for the rewards
		} else if (tick == 16) {
			rewards(true); // four seconds later, what the dropdowns offer
		} else if (tick == 40) {
			timer->stop();
			timer->deleteLater();
		}
	});
	timer->start(250);
}

// Inside OBS, with SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PRESS=<button>:<plugin> ("Install:slinglive",
// "Uninstall:slinglive", "Update:plinkolive"), or "-" to press nothing: once the list is there, says in the log what
// the dock shows for every plugin, presses that button in the dock the way a click does, and once it has settled says
// again what the dock shows. With SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PICTURES, it paints the dock and the dropdown.
// "Connect:twitch" connects Twitch instead (connectInObs).
void pressInObs(ManagerDock &dock, Manager &manager, const QString &pressing, const QString &pictures)
{
	const QString which = pressing.section(':', 0, 0), id = pressing.section(':', 1);
	if (which == "Connect" && id == "twitch") {
		connectInObs(dock, manager);
		return;
	}
	const auto describe = [&dock, &manager](const char *when) {
		for (QAction *action : dropdown(dock) ? dropdown(dock)->actions() : QList<QAction *>()) {
			auto *widget = qobject_cast<QWidgetAction *>(action);
			if (!widget || !widget->defaultWidget() || !widget->defaultWidget()->objectName().startsWith("plugin:"))
				continue;
			const QString plugin = widget->defaultWidget()->objectName().mid(7);
			const QString update = button(dock, plugin, "update");
			test::report(true, "in OBS, %s: %s: [%s]%s%s%s", when, plugin.toUtf8().constData(),
				     button(dock, plugin).toUtf8().constData(),
				     update == "(hidden)" ? "" : (" [" + update + "]").toUtf8().constData(),
				     note(dock, plugin).isEmpty() ? "" : (" " + note(dock, plugin)).toUtf8().constData(),
				     manager.store().running(plugin) ? ", running" : "");
		}
		test::report(true, "in OBS, %s: the dock says \"%s\"", when,
			     dockSays(dock, text("Restart")) ? "Restart OBS to finish." : "nothing to finish");
	};
	const auto paint = [&dock, pictures](const char *name) {
		if (pictures.isEmpty() || !QDir().mkpath(pictures))
			return;
		const QString file = QDir(pictures).filePath(QStringLiteral("in-obs-%1.png").arg(QLatin1String(name)));
		test::report(dock.grab().save(file, "PNG"), "a picture of the dock in %s", file.toUtf8().constData());
		saveDropdown(dock, QDir(pictures).filePath(QStringLiteral("in-obs-%1-dropdown.png").arg(QLatin1String(name))));
	};

	auto *timer = new QTimer(&dock);
	auto *step = new int(0);
	QObject::connect(timer, &QTimer::timeout, &dock, [=, &dock, &manager] {
		if (*step == 0) {
			if (manager.store().list() == Store::List::Waiting)
				return;
			describe("before");
			paint("before");
			if (which == "-" || id.isEmpty()) {
				timer->stop();
				return;
			}
			const QString wanted = text(which == "Update" ? "Update" : which == "Uninstall" ? "Uninstall" : "Install");
			const char *kind = which == "Update" ? "update" : "install";
			const QString showing = button(dock, id, kind);
			const bool right = which == "Update" ? showing.startsWith(wanted.section(' ', 0, 0)) : showing == wanted;
			test::report(right, "in OBS: pressing %s on %s (the button says %s)", which.toUtf8().constData(),
				     id.toUtf8().constData(), showing.toUtf8().constData());
			if (right)
				press(dock, id, kind);
			*step = 1;
		} else if (!manager.store().state(id).busy) {
			describe("after");
			paint("after");
			timer->stop();
			delete step;
			timer->deleteLater();
		}
	});
	timer->start(250);
}
