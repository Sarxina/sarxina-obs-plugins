#include "chatsaga.hpp"

#include "plain-text.hpp"

#include <obs-module.h>

#include <QDateTime>

namespace sarxina::chatsaga {

namespace {

constexpr int kStatusEveryMs = 2000; // the settings' line is drawn again at most this often

// The three mercenaries of mercenary mode. Their ids have a colon, which no Twitch login has.
struct Mercenary {
	const char *id;
	const char *name;
	const char *dialogue;
	int classId; // in the game's Classes.json
};
constexpr Mercenary kMercenaries[] = {
	{"mercenary:brannoc", "Brannoc", "Coin first. Glory after.", 1},
	{"mercenary:sela", "Sela", "I patch wounds. Try not to collect them.", 3},
	{"mercenary:tamsin", "Tamsin", "I set things on fire, for a fee.", 2},
};

ChatSaga *self(void *data)
{
	return static_cast<ChatSaga *>(data);
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString utf8(const char *text)
{
	return QString::fromUtf8(text ? text : "");
}

sarxina_plugin_info info = {
	SARXINA_OBS_PLUGIN_MANAGER_API,
	PLUGIN_NAME,
	PLUGIN_TITLE,
	nullptr, // the description, from the strings
	nullptr, // the plugin, set in start()
	[](void *data, bool enabled) { self(data)->setEnabled(enabled); },
	[](void *data) { return self(data)->properties(); },
	nullptr, // nothing to set: the one line only says how things are
	nullptr,
	nullptr, // no commands: everyone in chat is in town
	0,
	[](void *data, const sarxina_chat_message *message) { self(data)->chat(*message); },
};

} // namespace

ChatSaga::ChatSaga(const QString &profiles, const QString &avatars, TwitchSend twitch)
	: presence_(static_cast<double>(QDateTime::currentMSecsSinceEpoch())), twitch_(twitch), profiles_(profiles),
	  avatars_(avatars, QStringLiteral("http://127.0.0.1:%1/avatars/").arg(kPort), twitch)
{
	server_.setAvatars(avatars);
	avatars_.changed = [this] { sync(); };
	twitch_.broadcaster = [this](const QString &login) { presence_.setBroadcaster(login); };
	twitch_.live = [this](std::optional<double> since) {
		presence_.setLiveSince(since);
		sync();
	};
	twitch_.listed = [this](std::optional<std::vector<Chatter>> chatters) {
		presence_.setListed(std::move(chatters));
		sync();
	};
	twitch_.problemChanged = [this] { showStatus(); };
	tick_.setInterval(kSyncMs);
	connect(&tick_, &QTimer::timeout, this, &ChatSaga::sync);
	statusLater_.setSingleShot(true);
	connect(&statusLater_, &QTimer::timeout, this, &ChatSaga::showStatus);
}

void ChatSaga::start()
{
	info.description = obs_module_text("Description");
	info.data = this;
	if (sarxina_obs_plugin_manager_register(&info)) {
		blog(LOG_INFO, "[%s] %s loaded, run by the Sarxina OBS Plugin Manager", PLUGIN_TITLE, PLUGIN_VERSION);
		return;
	}
	blog(LOG_WARNING, "[%s] %s loaded, but the Sarxina OBS Plugin Manager is not installed: no chat", PLUGIN_TITLE,
	     PLUGIN_VERSION);
	enabled_ = true;
	turnOn();
}

void ChatSaga::runForTest(quint16 port)
{
	port_ = port;
	managed_ = true;
	enabled_ = true;
	turnOn();
}

void ChatSaga::shutDown()
{
	closed_ = true;
	turnOff();
}

// ---- on and off -----------------------------------------------------------------------------------------------------

void ChatSaga::setEnabled(bool enabled)
{
	managed_ = true; // only the manager calls this
	enabled_ = enabled;
	if (enabled)
		turnOn();
	else
		turnOff();
	blog(LOG_INFO, "[%s] %s", PLUGIN_TITLE, enabled ? "on" : "off");
}

// The town opens for the game, and chat is followed.
void ChatSaga::turnOn()
{
	if (closed_)
		return;
	if (server_.start(port_))
		blog(LOG_INFO, "[%s] the town is open on 127.0.0.1:%d", PLUGIN_TITLE, port_);
	else
		blog(LOG_WARNING, "[%s] the town cannot open on 127.0.0.1:%d: %s", PLUGIN_TITLE, port_,
		     server_.error().toUtf8().constData());
	if (managed_ && !testList_)
		twitch_.start();
	tick_.start();
	syncMercenaries();
	showStatus();
}

void ChatSaga::turnOff()
{
	twitch_.stop();
	tick_.stop();
	statusLater_.stop();
	server_.stop();
}

// ---- chat -----------------------------------------------------------------------------------------------------------

void ChatSaga::chat(const sarxina_chat_message &message)
{
	if (closed_ || !enabled_)
		return;
	const Chatter chatter{utf8(message.user_name), chatterName(message), utf8(message.user_id)};
	if (command(chatter, utf8(message.text)))
		return;
	if (message.role == SARXINA_ROLE_BROADCASTER) {
		presence_.setBroadcaster(chatter.login);
		return;
	}
	presence_.heard(chatter, utf8(message.text), static_cast<double>(message.time_ms));
	sync();
}

// "!chatsaga status" shows the chatter's status on stream for a moment, with no answer in chat; "!chatsaga changeclass
// <class>" changes their class. True when the message was one of ChatSaga's commands, which never become dialogue.
bool ChatSaga::command(const Chatter &chatter, const QString &text)
{
	const QStringList words = text.simplified().split(' ', Qt::SkipEmptyParts);
	if (words.isEmpty() || words[0].compare(QStringLiteral("!chatsaga"), Qt::CaseInsensitive) != 0)
		return false;
	const QString what = words.value(1).toLower();
	if (what == QStringLiteral("status")) {
		town_.askStatus(npcId(chatter.login));
	} else if (what == QStringLiteral("changeclass")) {
		const int classId = classNamed(words.mid(2).join(' '));
		if (classId) {
			profiles_.setClass(profileId(chatter.login), classId);
			sync();
		}
	}
	return true;
}

void ChatSaga::setMercenaries(bool on)
{
	if (on == mercenaries_)
		return;
	mercenaries_ = on;
	blog(LOG_INFO, "[%s] mercenary mode %s", PLUGIN_TITLE, on ? "on" : "off");
	syncMercenaries();
}

// The mercenaries are in town exactly while mercenary mode is on.
void ChatSaga::syncMercenaries()
{
	for (const Mercenary &mercenary : kMercenaries) {
		const QString id = QString::fromUtf8(mercenary.id);
		if (mercenaries_ && enabled_ && !closed_)
			town_.put(id, {QString::fromUtf8(mercenary.name), QString::fromUtf8(mercenary.dialogue), 0,
				       mercenary.classId, QString()});
		else
			town_.remove(id);
	}
}

void ChatSaga::listChattersForTest(const std::vector<Chatter> &chatters)
{
	testList_ = true;
	twitch_.stop();
	presence_.setListed(chatters);
	sync();
}

// Puts chat's NPCs in town as chat has them now, and takes out the ones chat put there that are no longer in it.
void ChatSaga::sync()
{
	if (closed_ || !enabled_)
		return;
	QSet<QString> now;
	std::vector<QString> userIds;
	for (const Npc &npc : presence_.npcs(this->now())) {
		const QString userId = profileId(npc.id);
		town_.put(npc.id, {npc.name, npc.dialogue, npc.spokeAt, profiles_.classFor(userId), avatars_.addressFor(userId)});
		now.insert(npc.id);
		userIds.push_back(userId);
	}
	avatars_.want(userIds);
	for (const QString &id : std::as_const(fromChat_))
		if (!now.contains(id))
			town_.remove(id);
	fromChat_ = now;
	if (!statusLater_.isActive())
		statusLater_.start(kStatusEveryMs);
}

// The id a chatter's profile and avatar are kept by: their Twitch user id, or their login while it is not known.
QString ChatSaga::profileId(const QString &login) const
{
	const QString userId = presence_.userIdOf(login);
	return userId.isEmpty() ? login : userId;
}

double ChatSaga::now() const
{
	return static_cast<double>(QDateTime::currentMSecsSinceEpoch());
}

// ---- settings -------------------------------------------------------------------------------------------------------

QString ChatSaga::status() const
{
	if (!server_.running())
		return text("Status.Closed").arg(port_).arg(server_.error());
	int lurking = 0;
	for (const Npc &npc : town_.npcs())
		lurking += npc.dialogue == kLurking;
	QString line = text("Status.Town").arg(town_.npcs().size()).arg(lurking);
	if (managed_ && !testList_ && !twitch_.problem().isEmpty())
		line += '\n' + text("Status.Twitch").arg(plainText(twitch_.problem(), 200));
	return line;
}

obs_properties_t *ChatSaga::properties() const
{
	obs_properties_t *properties = obs_properties_create();
	const QByteArray line = status().toUtf8();
	obs_properties_add_text(properties, "status", line.constData(), OBS_TEXT_INFO);
	return properties;
}

// The manager draws the settings again when the line changes, if they are open.
void ChatSaga::showStatus()
{
	const QString line = status();
	if (line == shownStatus_)
		return;
	shownStatus_ = line;
	if (managed_ && obs_initialized())
		sarxina_obs_plugin_manager_settings_changed(PLUGIN_NAME);
}

} // namespace sarxina::chatsaga
