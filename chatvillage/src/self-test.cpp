// ChatVillage's self-test (see shared/src/self-test.hpp). It needs no OBS, no Twitch and no game:
//   1. NPCs' ids, names and dialogue, and the town: NPCs put in, renamed, given new dialogue and taken out, with the
//      game told each time.
//   2. Who of chat is in town: Twitch's list, names, dialogue that goes from *lurking* to what they said and on to what
//      they said next, "this stream" before and after going live, the broadcaster and bots left out, chatters who
//      leave, and chat without Twitch's list.
//   3. Twitch asked through a stand-in for the manager, the way Twitch's documentation answers: the broadcaster, the
//      stream, chatters over two pages, and the manager or Twitch saying no.
//   4. The local trigger and feed over real connections on this computer: every route, refusals, and a feed that sees
//      the town and each change; and the town's page: its files with their types, and nothing outside its folder.
//   5. The whole plugin: chat and Twitch's list making NPCs that a feed sees come, change what they say and go.
#include "self-test.hpp"

#include "chat-presence.hpp"
#include "chatvillage.hpp"
#include "town-server.hpp"
#include "town.hpp"
#include "twitch-chatters.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QUrlQuery>

#include <functional>
#include <map>
#include <string>

using namespace sarxina;
using namespace sarxina::chatvillage;

namespace {

constexpr quint16 kTestPort = 47901; // away from the town's own port, which OBS may have open

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

// Lets the event loop run until `done` or two seconds pass.
bool waitFor(const std::function<bool()> &done)
{
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < 2000)
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	return done();
}

// The NPC with that id, or one named "(not there)".
Npc npcIn(const std::vector<Npc> &npcs, const QString &id)
{
	for (const Npc &npc : npcs)
		if (npc.id == id)
			return npc;
	return {id, QStringLiteral("(not there)"), {}};
}

QString dialogueOf(const std::vector<Npc> &npcs, const QString &id)
{
	return npcIn(npcs, id).dialogue;
}

bool isThere(const std::vector<Npc> &npcs, const QString &id)
{
	return npcIn(npcs, id).name != QLatin1String("(not there)");
}

// ---- 1. the town ----------------------------------------------------------------------------------------------------

bool checkTown()
{
	Tally tally;
	tally.expect(npcId(QStringLiteral("  Sarxina ")) == QLatin1String("sarxina"), "id trimmed, any case");
	tally.expect(npcId(QString()).isEmpty(), "no id");
	tally.expect(npcId(QString(32, 'a')).size() == 32, "32 characters");
	tally.expect(npcId(QString(33, 'a')).isEmpty(), "33 characters");
	tally.expect(npcId(QStringLiteral("a") + QChar(0x202E) + QStringLiteral("b")) == QLatin1String("ab"),
		     "characters that turn text around");
	tally.expect(npcName({}, QStringLiteral("bob")) == QLatin1String("bob"), "name from id");
	tally.expect(npcName(QString(40, 'x'), {}).size() == kLongestName, "name cut");
	tally.expect(npcDialogue(QString(600, 'x')).size() == kLongestDialogue, "dialogue cut");
	tally.expect(npcDialogue(QStringLiteral("hi") + QChar(0x202E)) == QLatin1String("hi"),
		     "dialogue without characters that turn text around");

	Town town;
	std::vector<QString> told;
	town.shown = [&told](const Npc &npc) { told.push_back("shown " + npc.id + "=" + npc.name + ":" + npc.dialogue); };
	town.gone = [&told](const QString &id) { told.push_back("gone " + id); };
	tally.expect(town.put("bob", {"Bob", "*lurking*"}) == Town::Put::Added, "added");
	tally.expect(town.put("ann", {}) == Town::Put::Added, "second added, with nothing given");
	tally.expect(town.put("bob", {"Bob", "*lurking*"}) == Town::Put::Unchanged, "same again");
	tally.expect(town.put("bob", {std::nullopt, "hello"}) == Town::Put::Changed, "new dialogue, name kept");
	tally.expect(town.put("bob", {"Bobby", std::nullopt}) == Town::Put::Changed, "new name, dialogue kept");
	tally.expect(town.remove("ann") && !town.remove("ann"), "removed once");
	tally.expect(town.npcs().size() == 1 && town.npcs()[0] == Npc{"bob", "Bobby", "hello"}, "what is left");
	tally.expect(told == std::vector<QString>{"shown bob=Bob:*lurking*", "shown ann=ann:", "shown bob=Bob:hello",
						  "shown bob=Bobby:hello", "gone ann"},
		     "the game told of each change");
	return tally.report("town", "checks as expected");
}

// ---- 2. who of chat is in town --------------------------------------------------------------------------------------

bool checkPresence()
{
	Tally tally;
	const double start = 1'000'000;
	ChatPresence presence(start);
	presence.setBroadcaster(QStringLiteral("Sarxina"));
	presence.setListed(std::vector<Chatter>{{"sarxina", "Sarxina"}, {"lurker", "Lurker"}, {"nightbot", "Nightbot"},
						 {"talker", "Talker"}});
	std::vector<Npc> npcs = presence.npcs(start + 1000);
	tally.expect(npcs.size() == 2, "broadcaster and bot left out");
	tally.expect(npcIn(npcs, "talker").name == QLatin1String("Talker"), "named after the chatter");
	tally.expect(dialogueOf(npcs, "lurker") == kLurking && dialogueOf(npcs, "talker") == kLurking, "listed: lurking");

	presence.heard({"talker", "TaLKer"}, QStringLiteral("hello town"), start + 2000);
	npcs = presence.npcs(start + 2000);
	tally.expect(dialogueOf(npcs, "talker") == QLatin1String("hello town"), "chatted: says it");
	tally.expect(npcIn(npcs, "talker").name == QLatin1String("TaLKer"), "named as chat shows them now");
	presence.heard({"talker", "TaLKer"}, QStringLiteral("anyone here?"), start + 2500);
	tally.expect(dialogueOf(presence.npcs(start + 2500), "talker") == QLatin1String("anyone here?"),
		     "chatted again: says the newer line");
	presence.heard({"talker", "TaLKer"}, QStringLiteral("late and old"), start + 2400);
	tally.expect(dialogueOf(presence.npcs(start + 2500), "talker") == QLatin1String("anyone here?"),
		     "an older line arriving late changes nothing");
	presence.heard({"newcomer", "Newcomer"}, QStringLiteral("first!"), start + 3000);
	tally.expect(dialogueOf(presence.npcs(start + 3000), "newcomer") == QLatin1String("first!"),
		     "chatted before Twitch lists them: there, saying it");

	presence.setLiveSince(start + 5000);
	npcs = presence.npcs(start + 6000);
	tally.expect(dialogueOf(npcs, "talker") == kLurking, "chatted only before the stream went live: lurking");
	presence.heard({"talker", "TaLKer"}, QStringLiteral("we're live"), start + 7000);
	tally.expect(dialogueOf(presence.npcs(start + 7000), "talker") == QLatin1String("we're live"),
		     "chatted this stream");

	presence.setListed(std::vector<Chatter>{{"talker", "TaLKer"}});
	npcs = presence.npcs(start + 8000);
	tally.expect(!isThere(npcs, "lurker"), "left chat: gone");
	tally.expect(isThere(npcs, "newcomer") && dialogueOf(npcs, "newcomer") == kLurking,
		     "not listed yet, chatted a moment ago, before going live");
	npcs = presence.npcs(start + 3000 + kStillHereMs + 1);
	tally.expect(!isThere(npcs, "newcomer"), "not listed, quiet too long: gone");

	presence.setListed(std::nullopt);
	npcs = presence.npcs(start + 7000 + 1000);
	tally.expect(npcs.size() == 2 && dialogueOf(npcs, "talker") == QLatin1String("we're live"),
		     "without Twitch's list: those who chatted a moment ago");
	return tally.report("chat", "checks as expected");
}

// ---- 3. Twitch through a stand-in for the manager -------------------------------------------------------------------

// Answers like Twitch's documentation, later, as the manager does. `refuse` answers every request with that status.
struct StandInTwitch {
	int refuse = 0;
	QString why;
	bool live = true;
	std::vector<QString> asked;

	TwitchSend send()
	{
		return [this](const char *, const QString &path, const QByteArray &, TwitchAnswer done) {
			asked.push_back(path);
			const auto [status, answer] = answerTo(path);
			QMetaObject::invokeMethod(
				QCoreApplication::instance(), [done, status = status, answer = answer] { done(status, answer); },
				Qt::QueuedConnection);
			return true;
		};
	}

	std::pair<int, QJsonObject> answerTo(const QString &path) const
	{
		if (refuse)
			return {refuse, {{"message", why}}};
		if (path == QLatin1String("/channels"))
			return {200, data({QJsonObject{{"broadcaster_id", "141981764"}, {"broadcaster_login", "sarxina"}}})};
		if (path.startsWith(QLatin1String("/streams")))
			return {200, live ? data({QJsonObject{{"started_at", "2026-10-25T18:00:00Z"}}}) : data({})};
		const QUrlQuery query(path.section('?', 1));
		if (query.queryItemValue("after") == QLatin1String("page2"))
			return {200, data({chatter("ann")})};
		QJsonObject first = data({chatter("sarxina"), chatter("bob")});
		first.insert("pagination", QJsonObject{{"cursor", "page2"}});
		return {200, first};
	}

	static QJsonObject chatter(const char *login)
	{
		return {{"user_id", "1"}, {"user_login", login}, {"user_name", QString::fromLatin1(login).toUpper()}};
	}
	static QJsonObject data(const QJsonArray &items) { return {{"data", items}}; }
};

bool checkTwitch()
{
	Tally tally;
	StandInTwitch stand;
	TwitchChatters twitch(stand.send());
	QString broadcaster;
	std::optional<double> since;
	std::optional<std::vector<Chatter>> listed;
	int lists = 0;
	twitch.broadcaster = [&broadcaster](const QString &login) { broadcaster = login; };
	twitch.live = [&since](std::optional<double> ms) { since = ms; };
	twitch.listed = [&](std::optional<std::vector<Chatter>> chatters) {
		listed = std::move(chatters);
		lists++;
	};
	twitch.start(60 * 60 * 1000);
	waitFor([&lists] { return lists == 1; });
	tally.expect(broadcaster == QLatin1String("sarxina"), "the broadcaster");
	tally.expect(since && *since == QDateTime::fromString("2026-10-25T18:00:00Z", Qt::ISODate).toMSecsSinceEpoch(),
		     "live since");
	tally.expect(listed && listed->size() == 3 && listed->at(2).login == QLatin1String("ann"), "chatters, two pages");
	tally.expect(stand.asked.size() == 4 && stand.asked[1] == QLatin1String("/streams?user_id=141981764") &&
			     stand.asked[2].contains("moderator_id=141981764") && stand.asked[3].contains("after=page2"),
		     "what was asked");
	tally.expect(twitch.problem().isEmpty(), "no problem");

	stand.refuse = 401;
	stand.why = QStringLiteral("Missing scope: moderator:read:chatters");
	stand.live = false;
	twitch.stop();
	twitch.start(60 * 60 * 1000);
	waitFor([&lists] { return lists == 2; });
	tally.expect(!listed, "Twitch says no: no list");
	tally.expect(twitch.problem() == stand.why, "in Twitch's words");

	stand.refuse = 0;
	twitch.stop();
	twitch.start(60 * 60 * 1000);
	waitFor([&lists] { return lists == 3; });
	tally.expect(listed && twitch.problem().isEmpty() && !since, "back, and not live");
	return tally.report("Twitch", "checks as expected");
}

// ---- 4. the trigger and the feed ------------------------------------------------------------------------------------

struct Reply {
	int status = 0;
	QJsonObject body;
	QByteArray content; // the body as it came
	QByteArray type;    // its Content-Type
};

// The value of one header of an answer.
QByteArray header(const QByteArray &head, const QByteArray &name)
{
	for (const QByteArray &line : head.split('\n'))
		if (line.toLower().startsWith(name.toLower() + ':'))
			return line.mid(name.size() + 1).trimmed();
	return {};
}

// One request to the server on this computer, answered while the event loop runs.
Reply request(const QByteArray &method, const QByteArray &path, const QByteArray &body = {},
	      const QByteArray &host = "127.0.0.1:" + QByteArray::number(kTestPort))
{
	QTcpSocket socket;
	socket.connectToHost(QStringLiteral("127.0.0.1"), kTestPort);
	QByteArray got;
	QObject::connect(&socket, &QTcpSocket::readyRead, [&] { got += socket.readAll(); });
	socket.write(method + ' ' + path + " HTTP/1.1\r\nHost: " + host + "\r\nContent-Length: " +
		     QByteArray::number(body.size()) + "\r\n\r\n" + body);
	waitFor([&socket] { return socket.state() == QAbstractSocket::UnconnectedState; });
	const qsizetype end = got.indexOf("\r\n\r\n");
	const QByteArray content = got.mid(end + 4);
	return {got.mid(9, 3).toInt(), QJsonDocument::fromJson(content).object(), content,
		header(got.left(end), "Content-Type")};
}

// A game's feed: every event it has had, as "name {json}".
struct Feed {
	QTcpSocket socket;
	QByteArray got;

	Feed()
	{
		socket.connectToHost(QStringLiteral("127.0.0.1"), kTestPort);
		QObject::connect(&socket, &QTcpSocket::readyRead, [this] { got += socket.readAll(); });
		socket.write("GET /npcs/events HTTP/1.1\r\nHost: localhost:" + QByteArray::number(kTestPort) + "\r\n\r\n");
	}
	bool saw(const QByteArray &event) { return waitFor([this, &event] { return got.contains(event); }); }
};

// The town's page: a folder with a page and a script in it, and a file beside the folder that must stay out of reach.
void checkPage(TownServer &server, Tally &tally)
{
	QTemporaryDir place;
	QDir(place.path()).mkpath(QStringLiteral("game/js"));
	const auto write = [&place](const char *name, const QByteArray &content) {
		QFile file(place.filePath(QString::fromLatin1(name)));
		if (file.open(QIODevice::WriteOnly))
			file.write(content);
	};
	write("game/index.html", "<title>town</title>");
	write("game/js/main.js", "start();");
	write("secret.txt", "not for the page");
	server.setPage(place.filePath(QStringLiteral("game")));

	Reply reply = request("GET", "/");
	tally.expect(reply.status == 200 && reply.content == "<title>town</title>" && reply.type.startsWith("text/html"),
		     "the page at /");
	reply = request("GET", "/js/main.js");
	tally.expect(reply.status == 200 && reply.content == "start();" && reply.type.startsWith("text/javascript"),
		     "a script, with its type");
	tally.expect(request("GET", "/js/missing.js").status == 404, "a file that is not there");
	tally.expect(request("GET", "/../secret.txt").status == 404, "a file above the folder");
	tally.expect(request("GET", "/%2e%2e/secret.txt").status == 404, "above the folder, URL-encoded");
	tally.expect(request("GET", "/..%5Csecret.txt").status == 404, "above the folder, with a backslash");
	tally.expect(request("PUT", "/index.html").status == 405, "the page cannot be written");
}

bool checkServer()
{
	Tally tally;
	Town town;
	TownServer server(town);
	tally.expect(server.start(kTestPort), "listens");
	Feed feed;
	tally.expect(feed.saw("event: town\ndata: {\"npcs\":[]}"), "feed: the town, empty");

	Reply reply = request("PUT", "/npcs/Bob%20Ross", R"({"name": "Bob Ross", "dialogue": "*lurking*"})");
	tally.expect(reply.status == 201 && reply.body["id"] == "bob ross" && reply.body["name"] == "Bob Ross" &&
			     reply.body["dialogue"] == "*lurking*",
		     "added");
	tally.expect(feed.saw("event: npc\ndata: {\"dialogue\":\"*lurking*\",\"id\":\"bob ross\",\"name\":\"Bob Ross\"}"),
		     "feed: it came");
	reply = request("PUT", "/npcs/BOB%20ROSS", R"({"dialogue": "happy little trees"})");
	tally.expect(reply.status == 200 && reply.body["added"] == false && reply.body["name"] == "Bob Ross" &&
			     reply.body["dialogue"] == "happy little trees",
		     "new dialogue, name kept");
	tally.expect(feed.saw("{\"dialogue\":\"happy little trees\",\"id\":\"bob ross\",\"name\":\"Bob Ross\"}"),
		     "feed: new dialogue");
	reply = request("PUT", "/npcs/ann");
	tally.expect(reply.status == 201 && reply.body["name"] == "ann" && reply.body["dialogue"] == "",
		     "no body: named after the id, saying nothing");
	reply = request("GET", "/npcs");
	tally.expect(reply.status == 200 && reply.body["npcs"].toArray().size() == 2, "the list");
	reply = request("DELETE", "/npcs/ann");
	tally.expect(reply.status == 200 && reply.body["removed"] == true, "removed");
	tally.expect(feed.saw("event: gone\ndata: {\"id\":\"ann\"}"), "feed: it went");
	tally.expect(request("DELETE", "/npcs/ann").status == 404, "removed again: not there");

	tally.expect(request("PUT", "/npcs/x", "not json").status == 400, "a body that is not JSON");
	tally.expect(request("PUT", "/npcs/x", R"({"name": 5})").status == 400, "a name that is not text");
	tally.expect(request("PUT", "/npcs/x", R"({"dialogue": []})").status == 400, "dialogue that is not text");
	tally.expect(request("PUT", "/npcs/" + QByteArray(40, 'a')).status == 400, "an id too long");
	tally.expect(request("POST", "/npcs/x").status == 405, "another method");
	tally.expect(request("GET", "/elsewhere").status == 404, "another path");
	tally.expect(request("PUT", "/npcs/x", {}, "evil.example:" + QByteArray::number(kTestPort)).status == 403,
		     "another host");
	tally.expect(!town.find("x"), "nothing refused got in");
	tally.expect(server.games() == 1, "one game drawing");
	checkPage(server, tally);
	server.stop();
	return tally.report("trigger and feed", "checks as expected");
}

// ---- 5. the whole plugin --------------------------------------------------------------------------------------------

bool checkPlugin()
{
	Tally tally;
	StandInTwitch stand;
	stand.live = false;
	ChatVillage plugin(stand.send());
	plugin.runForTest(kTestPort);
	Feed feed;
	tally.expect(feed.saw("{\"dialogue\":\"*lurking*\",\"id\":\"bob\",\"name\":\"BOB\"}") &&
			     feed.saw("{\"dialogue\":\"*lurking*\",\"id\":\"ann\",\"name\":\"ANN\"}"),
		     "Twitch's list: lurkers, named as Twitch shows them");
	tally.expect(!plugin.town().find("sarxina"), "the broadcaster is the player");

	sarxina_chat_message message{};
	message.user_name = "bob";
	message.display_name = "Bob";
	message.text = "hi";
	message.time_ms = QDateTime::currentMSecsSinceEpoch();
	plugin.chat(message);
	tally.expect(feed.saw("{\"dialogue\":\"hi\",\"id\":\"bob\",\"name\":\"Bob\"}"),
		     "chatted: says it, the moment they chat");
	message.text = "how's everyone";
	message.time_ms += 1000;
	plugin.chat(message);
	tally.expect(feed.saw("{\"dialogue\":\"how's everyone\",\"id\":\"bob\",\"name\":\"Bob\"}"),
		     "chatted again: says the newer line");

	plugin.listChattersForTest({{"ann", "Ann"}});
	tally.expect(plugin.town().find("bob") != nullptr, "just chatted, not listed: stays");
	tally.expect(plugin.town().find("ann") && plugin.town().npcs().size() == 2, "the town now");
	tally.expect(plugin.status().contains(QLatin1String("2")), "the status line counts them");
	plugin.shutDown();
	return tally.report("plugin", "checks as expected");
}

} // namespace

bool runSelfTest(const char *)
{
	bool ok = checkTown();
	ok = checkPresence() && ok;
	ok = checkTwitch() && ok;
	ok = checkServer() && ok;
	ok = checkPlugin() && ok;
	test::report(ok, "%s", ok ? "passed" : "FAILED");
	return ok;
}
