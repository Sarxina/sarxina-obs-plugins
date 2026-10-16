// The Sarxina OBS Plugin Manager without its dock: the plugins that handed themselves to it and whether each is added
// to OBS, the chat connection and the VTube Studio connection they share, and what it remembers between sessions (in
// its config.json). A plugin it has not seen before stays off until the streamer adds it.
#pragma once

#include "chat-router.hpp"
#include "plugin.hpp"
#include "twitch-chat.hpp"
#include "vtube-studio.hpp"

#include <QElapsedTimer>

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
		SelfTest, // reads and writes no files
	};
	explicit Manager(Use use = Use::Obs, const QString &twitchServer = TwitchChat::kServer);

	// What plugins call (sarxina-obs-plugin-manager.h).
	bool add(const sarxina_plugin_info *info);
	void settingsChanged(const char *id);
	void vtsRequest(const sarxina_vts_request &request);

	const std::vector<std::unique_ptr<Plugin>> &plugins() const { return plugins_; }
	void setEnabled(Plugin &plugin, bool enabled);

	TwitchChat &twitch() { return twitch_; }
	const QString &channel() const { return channel_; }
	void joinTwitch(const QString &channel);
	void leaveTwitch();

	VTubeStudio &vts() { return vts_; }
	int vtsPort() const { return vtsPort_; }
	void connectVts(int port); // may make VTube Studio ask the streamer to allow the manager
	void disconnectVts();

	// Connects again whatever was connected when OBS last closed.
	void start();
	// OBS is closing: the connections close, and nothing more reaches any plugin.
	void shutDown();

signals:
	void pluginRegistered(sarxina::Plugin *plugin);
	void pluginChanged(sarxina::Plugin *plugin); // added or removed, or its settings changed

private:
	void route(const ChatMessage &message);
	void load();
	void save() const;

	Use use_;
	std::vector<std::unique_ptr<Plugin>> plugins_;
	ChatRouter router_;
	TwitchChat twitch_;
	VTubeStudio vts_;
	QElapsedTimer clock_;
	bool closed_ = false;

	// Remembered between sessions
	std::map<std::string, bool> enabled_;
	QString channel_;
	bool chatOn_ = false;
	int vtsPort_ = 8001;
	bool vtsOn_ = false;
	QString vtsToken_;
};

} // namespace sarxina
