// The channel as Twitch sees it, asked every kPollMs through the Sarxina OBS Plugin Manager's Twitch connection: who
// the broadcaster is, whether the stream is live and since when, and everyone in chat now (Get Chatters, which needs
// the scope moderator:read:chatters). While Twitch cannot be asked, problem() says why in Twitch's or the manager's
// words, and there is no list.
#pragma once

#include "chat-presence.hpp"
#include "chat-trigger.hpp"

#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>
#include <vector>

namespace sarxina::chatvillage {

inline constexpr int kPollMs = 30 * 1000;

class TwitchChatters : public QObject {
public:
	// `send` reaches Twitch (sendToTwitch unless the self-test says otherwise).
	explicit TwitchChatters(TwitchSend send = sendToTwitch);

	std::function<void(const QString &login)> broadcaster;
	std::function<void(std::optional<double> sinceMs)> live; // nothing while the channel is not live
	std::function<void(std::optional<std::vector<Chatter>>)> listed;
	std::function<void()> problemChanged;

	// Asks now, then every `everyMs`.
	void start(int everyMs = kPollMs);
	void stop();
	// Why Twitch cannot be asked, or "" when it can.
	const QString &problem() const { return problem_; }

private:
	void poll();
	void askChannel();
	void askStream();
	void askChatters(const QString &after, std::vector<Chatter> sofar);
	void ask(const QString &path, std::function<void(const QJsonObject &)> then);
	bool answered(int status, const QJsonObject &answer);
	void setProblem(const QString &problem);

	TwitchSend send_;
	QTimer timer_;
	QString broadcasterId_;
	QString problem_;
	bool asking_ = false; // a round of questions is under way
};

} // namespace sarxina::chatvillage
