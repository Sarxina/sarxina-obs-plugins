#include "chatvillage.hpp"

#include "plain-text.hpp"

#include <obs-module.h>

#include <QDateTime>

namespace sarxina::chatvillage {

namespace {

constexpr int kStatusEveryMs = 2000; // the settings' line is drawn again at most this often

ChatVillage *self(void *data)
{
	return static_cast<ChatVillage *>(data);
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

ChatVillage::ChatVillage(TwitchSend twitch)
	: presence_(static_cast<double>(QDateTime::currentMSecsSinceEpoch())), twitch_(std::move(twitch))
{
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
	connect(&tick_, &QTimer::timeout, this, &ChatVillage::sync);
	statusLater_.setSingleShot(true);
	connect(&statusLater_, &QTimer::timeout, this, &ChatVillage::showStatus);
}

void ChatVillage::start()
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

void ChatVillage::runForTest(quint16 port)
{
	port_ = port;
	managed_ = true;
	enabled_ = true;
	turnOn();
}

void ChatVillage::shutDown()
{
	closed_ = true;
	turnOff();
}

// ---- on and off -----------------------------------------------------------------------------------------------------

void ChatVillage::setEnabled(bool enabled)
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
void ChatVillage::turnOn()
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
	showStatus();
}

void ChatVillage::turnOff()
{
	twitch_.stop();
	tick_.stop();
	statusLater_.stop();
	server_.stop();
}

// ---- chat -----------------------------------------------------------------------------------------------------------

void ChatVillage::chat(const sarxina_chat_message &message)
{
	if (closed_ || !enabled_)
		return;
	const Chatter chatter{utf8(message.user_name), chatterName(message)};
	if (message.role == SARXINA_ROLE_BROADCASTER) {
		presence_.setBroadcaster(chatter.login);
		return;
	}
	presence_.heard(chatter, utf8(message.text), static_cast<double>(message.time_ms));
	sync();
}

void ChatVillage::listChattersForTest(const std::vector<Chatter> &chatters)
{
	testList_ = true;
	twitch_.stop();
	presence_.setListed(chatters);
	sync();
}

// Puts chat's NPCs in town as chat has them now, and takes out the ones chat put there that are no longer in it.
void ChatVillage::sync()
{
	if (closed_ || !enabled_)
		return;
	QSet<QString> now;
	for (const Npc &npc : presence_.npcs(this->now())) {
		town_.put(npc.id, {npc.name, npc.dialogue});
		now.insert(npc.id);
	}
	for (const QString &id : std::as_const(fromChat_))
		if (!now.contains(id))
			town_.remove(id);
	fromChat_ = now;
	if (!statusLater_.isActive())
		statusLater_.start(kStatusEveryMs);
}

double ChatVillage::now() const
{
	return static_cast<double>(QDateTime::currentMSecsSinceEpoch());
}

// ---- settings -------------------------------------------------------------------------------------------------------

QString ChatVillage::status() const
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

obs_properties_t *ChatVillage::properties() const
{
	obs_properties_t *properties = obs_properties_create();
	const QByteArray line = status().toUtf8();
	obs_properties_add_text(properties, "status", line.constData(), OBS_TEXT_INFO);
	return properties;
}

// The manager draws the settings again when the line changes, if they are open.
void ChatVillage::showStatus()
{
	const QString line = status();
	if (line == shownStatus_)
		return;
	shownStatus_ = line;
	if (managed_ && obs_initialized())
		sarxina_obs_plugin_manager_settings_changed(PLUGIN_NAME);
}

} // namespace sarxina::chatvillage
