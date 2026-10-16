// Twitch chat, read the way Twitch lets anyone read it without logging in: as "justinfan" and some digits, over
// Twitch's chat websocket. Nothing is ever sent to the chat. Each chat message becomes a ChatMessage.
#pragma once

#include "chat.hpp"
#include "websocket.hpp"

#include <QElapsedTimer>
#include <QTimer>

#include <string>

namespace sarxina {

class TwitchChat : public QObject {
	Q_OBJECT

public:
	enum class State {
		Off,
		Connecting, // until Twitch says the channel is joined
		Reading,
		Retrying, // the connection dropped or could not be made
	};
	static constexpr const char *kServer = "wss://irc-ws.chat.twitch.tv:443";

	explicit TwitchChat(QString server = kServer, QObject *parent = nullptr);

	// A Twitch account name: 1 to 25 letters, digits and _.
	static bool validChannel(const QString &channel);
	// Reads this channel's chat (a valid name, any case), and keeps reading it until leave().
	void join(const QString &channel);
	void leave();
	State state() const { return state_; }
	const QString &channel() const { return channel_; }

	// One line of Twitch's chat protocol, if it is a chat message.
	static bool parse(const std::string &line, ChatMessage &message);

signals:
	void stateChanged();
	void message(const sarxina::ChatMessage &message);

private:
	void opened();
	void received(const QByteArray &text);
	void line(const std::string &line);
	void keepAlive();
	void setState(State state);

	WebSocket socket_;
	QString server_;
	QString channel_;
	std::string nick_;
	State state_ = State::Off;
	QTimer keepAlive_;
	QElapsedTimer heard_; // since Twitch last sent anything
	bool pinged_ = false;
};

} // namespace sarxina
