// Chatters' Twitch avatars, kept in a folder of the plugin's own and served by the town (town-server.hpp) so that the
// game can draw them. Each chatter's avatar is looked up once a run through the Sarxina OBS Plugin Manager's Twitch
// connection (Get Users' profile_image_url) and downloaded from Twitch's picture server; one already kept is used
// meanwhile, and the game draws a default for a chatter with none.
#pragma once

#include "chat-trigger.hpp"

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>
#include <vector>

class QNetworkAccessManager;

namespace sarxina::chatsaga {

inline constexpr int kLongestAvatar = 2 * 1024 * 1024; // bytes

// Downloads a picture; `done` gets its bytes, or nothing when it could not be had.
using Download = std::function<void(const QUrl &url, std::function<void(const QByteArray &)> done)>;

class Avatars : public QObject {
public:
	// Avatars kept in `folder` and served at `address` + "<user id>.png". `twitch` and `download` reach Twitch and its
	// picture server (sendToTwitch and a download of the plugin's own, unless the self-test says otherwise).
	Avatars(const QString &folder, const QString &address, TwitchSend twitch = sendToTwitch, Download download = {});

	// Told when an avatar has been kept, to show it.
	std::function<void()> changed;

	// Looks up the avatars of these users not asked about yet this run.
	void want(const std::vector<QString> &userIds);
	// The address the game loads the user's avatar from, or "" while there is none.
	QString addressFor(const QString &userId) const;

private:
	void lookUp(const std::vector<QString> &userIds);
	void fetch(const QString &userId, const QUrl &picture);
	void keep(const QString &userId, const QByteArray &picture);
	QString fileFor(const QString &userId) const;

	QString folder_;
	QString address_;
	TwitchSend twitch_;
	Download download_;
	QNetworkAccessManager *network_ = nullptr;
	QSet<QString> asked_;
};

// Whether the address is one of Twitch's pictures, the only place avatars are downloaded from.
bool isTwitchPicture(const QUrl &url);
// Whether the user id can name a file: digits, letters, - and _ only.
bool isFileSafeId(const QString &userId);

} // namespace sarxina::chatsaga
