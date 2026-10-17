// The Sarxina OBS Plugin Manager without its dock: the plugins that handed themselves to it, the store that installs
// and uninstalls plugins (store.hpp), the chat connection and the VTube Studio connection the plugins share, and what
// it remembers between sessions (in its config.json). A plugin that is installed runs; one uninstalled while OBS runs
// is switched off at once.
//
// The chat is the chat of the Twitch account OBS itself is connected to (Settings > Stream), read without logging in.
#pragma once

#include "chat-router.hpp"
#include "plugin.hpp"
#include "store.hpp"
#include "twitch-chat.hpp"
#include "vtube-studio.hpp"

#include <QElapsedTimer>

#include <util/config-file.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sarxina {

class Manager : public QObject {
	Q_OBJECT

public:
	enum class Use {
		Obs,      // the real one: remembers things in its config.json
		SelfTest, // reads and writes no config.json
	};
	explicit Manager(Use use = Use::Obs, const QString &twitchServer = TwitchChat::kServer,
			 Store::Setup store = Store::Setup());

	// What plugins call (sarxina-obs-plugin-manager.h).
	bool add(const sarxina_plugin_info *info);
	void settingsChanged(const char *id);
	void vtsRequest(const sarxina_vts_request &request);

	const std::vector<std::unique_ptr<Plugin>> &plugins() const { return plugins_; }
	Plugin *plugin(const QString &id) const;
	void setEnabled(Plugin &plugin, bool enabled);

	Store &store() { return store_; }

	TwitchChat &twitch() { return twitch_; }
	// Reads this account's chat, or none if it is empty.
	void followTwitchAccount(const QString &account);

	VTubeStudio &vts() { return vts_; }
	int vtsPort() const { return vtsPort_; }
	void connectVts(int port); // may make VTube Studio ask the streamer to allow the manager
	void disconnectVts();

	// OBS has finished loading: fetches the list of plugins, and connects VTube Studio again if it was connected when
	// OBS last closed.
	void start();
	// OBS is closing: the connections close, and nothing more reaches any plugin.
	void shutDown();

signals:
	void pluginRegistered(sarxina::Plugin *plugin);
	void pluginChanged(sarxina::Plugin *plugin); // switched on or off, or its settings changed

private:
	void route(const ChatMessage &message);
	void load();
	void save() const;

	Use use_;
	Store store_;
	std::vector<std::unique_ptr<Plugin>> plugins_;
	ChatRouter router_;
	TwitchChat twitch_;
	VTubeStudio vts_;
	QElapsedTimer clock_;
	bool closed_ = false;

	// Remembered between sessions
	int vtsPort_ = 8001;
	bool vtsOn_ = false;
	QString vtsToken_;
};

// The name of the Twitch account an OBS profile is connected to, or empty. Only the name is read: never the tokens
// OBS keeps beside it. OBS writes the account into the profile when it closes or switches profile.
QString twitchAccount(config_t *profile);

} // namespace sarxina
