// The KeepTalking leaderboard (sarxina.com/keeptalking): a stream that ended is sent there with the streamer's Twitch
// channel, its words and its length. The channel comes from Twitch's GET /channels through the Sarxina OBS Plugin
// Manager's Twitch connection; the stream goes to the leaderboard service as POST {kLeaderboardUrl}/submit (the
// contract: Skills And Context\vtuber-growth\keeptalking_leaderboard\CONTRACT.md). Every outcome is logged.
#pragma once

#include <QDateTime>
#include <QString>

#include <functional>

class QNetworkAccessManager;

namespace sarxina::keeptalking {

struct LeaderboardStream {
	long long words = 0;
	double seconds = 0; // Start Streaming to Stop Streaming
	QDateTime started;  // Start Streaming, UTC
};

struct LeaderboardResult {
	bool sent = false;
	QString line; // for the settings: "Sent: Long Stream, rank 3", "Not sent: connect Twitch in the manager"
};

// The words for a stream that is not sent, as the settings show it ("Not sent: <reason>").
QString notSent(const QString &reason);

// Sends the stream; `done` is called once, on the UI thread, unless `network` is deleted first. Call on the UI thread.
void submitToLeaderboard(const LeaderboardStream &stream, QNetworkAccessManager *network,
			 std::function<void(const LeaderboardResult &)> done);
// The test route's way: sends the stream as the given channel (login), without asking Twitch.
void submitAsChannel(const LeaderboardStream &stream, const QString &login, QNetworkAccessManager *network,
		     std::function<void(const LeaderboardResult &)> done);

} // namespace sarxina::keeptalking
