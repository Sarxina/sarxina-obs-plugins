// Requests to Twitch's servers, one at a time, on a thread of its own so that OBS never waits on the network. Made with
// libcurl, which OBS ships. The manager's channel point connection (twitch-api.hpp) sends everything it says to Twitch
// through it.
//
// Only HTTPS is spoken, and only to Twitch's two hosts for apps: id.twitch.tv, where an app logs in, and
// api.twitch.tv, Twitch's API. No redirect is followed. For tests, a server on 127.0.0.1 can stand in for both (the
// environment variable SARXINA_OBS_PLUGIN_MANAGER_TWITCH_TEST_SERVER), over plain http, and nothing else. An answer
// bigger than 1 MB is cut off, and a request that stalls for 30 seconds is given up.
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace sarxina {

// Where Twitch is.
struct TwitchHosts {
	QString id;         // where an app logs in: https://id.twitch.tv
	QString api;        // Twitch's API, Helix: https://api.twitch.tv/helix
	bool local = false; // a test server on this computer: plain http

	static TwitchHosts twitch();
	// http://127.0.0.1:<port>, standing in for both hosts; anything else gives nothing.
	static std::optional<TwitchHosts> testServer(const QString &address);
	// Twitch, unless SARXINA_OBS_PLUGIN_MANAGER_TWITCH_TEST_SERVER names a test server.
	static TwitchHosts forObs();
};

class TwitchHttp : public QObject {
	Q_OBJECT

public:
	struct Request {
		QByteArray method; // GET, POST, PATCH or DELETE
		QString url;       // has to start with one of the hosts
		QList<QByteArray> headers; // "Name: value"
		QByteArray body;           // or empty for none
	};
	// Called once, on the UI thread: Twitch's HTTP status and the body of its answer, whatever the status; or status 0
	// when no answer came (Twitch could not be reached, or the address is not Twitch's).
	using Done = std::function<void(int status, const QByteArray &body)>;

	static constexpr qint64 kLargestAnswer = 1 << 20; // bytes

	explicit TwitchHttp(TwitchHosts hosts, QObject *parent = nullptr);
	~TwitchHttp() override; // stops what is running; nothing more is called

	void send(Request request, Done done);
	// Drops every request that is waiting or under way: none of their Done is called.
	void forget();

	const TwitchHosts &hosts() const { return hosts_; }
	// Whether `url` is on one of these hosts.
	static bool allowed(const TwitchHosts &hosts, const QString &url);

private:
	struct Job {
		quint64 id;
		std::string method, url, body;
		std::vector<std::string> headers;
	};
	struct Transfer;

	void run();
	void perform(const Job &job);
	bool quitting();

	const TwitchHosts hosts_;

	std::mutex mutex_;
	std::condition_variable wake_;
	std::deque<Job> jobs_;
	bool quit_ = false;

	quint64 next_ = 1;                // UI thread only
	std::map<quint64, Done> waiting_; // UI thread only
	std::thread thread_;
};

} // namespace sarxina
