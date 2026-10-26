// The manager's one Twitch connection, for everything plugins do with Twitch: the chat read is the chat of the account
// connected here (twitch-chat.hpp reads it the way Twitch lets anyone read a channel's chat, so it needs no scope of
// its own), and plugins reach Twitch's API (Helix) through it as the broadcaster
// (sarxina_obs_plugin_manager_twitch_request). OBS's own Twitch account and tokens are never read.
//
// The streamer connects it in the dock with Twitch's device code flow, made for apps without a web page of their own:
// the manager asks Twitch for a code, the streamer enters it at twitch.tv/activate and allows the manager to read and
// manage channel point rewards (the scopes channel:read:redemptions and channel:manage:redemptions) and to see who is
// in chat (moderator:read:chatters), and meanwhile
// the manager asks Twitch every few seconds whether that has happened. The app is a public one, so it has no secret:
// only its id from dev.twitch.tv (SARXINA_TWITCH_CLIENT_ID, compiled in). A build without one cannot connect.
//
// The access token and the refresh token are kept in twitch.json, beside the manager's config.json, and never written
// to the log. Twitch wants a token checked when the app starts and every hour after. When Twitch answers a request
// with 401 (the token ran out, after four hours), the token is refreshed once and the request sent again; a refresh
// token works only once, so the new one is kept at once. When Twitch no longer takes the refresh token, or the tokens
// lack a scope, the connection needs reconnecting, and the dock says so.
#pragma once

#include "twitch-http.hpp"

#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <vector>

namespace sarxina {

class TwitchApi : public QObject {
	Q_OBJECT

public:
	struct Setup {
		TwitchHosts hosts = TwitchHosts::twitch();
		QString clientId;   // the app's Client ID on dev.twitch.tv; empty: this build cannot connect
		QString remembered; // the twitch.json to keep the tokens in, or empty for none
		std::function<void(const QUrl &)> open; // opens a page in the browser; empty: none is opened
		int secondMs = 1000; // how long one of Twitch's seconds is; the self-test makes them shorter

		// The id compiled in, unless SARXINA_OBS_PLUGIN_MANAGER_TWITCH_CLIENT_ID gives another (for tests).
		static Setup forObs();
	};

	enum class State {
		NoApp,      // this build has no Twitch app id
		Off,        // not connected
		Connecting, // asking Twitch for a code, or who the new token belongs to
		Waiting,    // for the streamer to enter the code at twitch.tv/activate
		Ready,      // connected as login()
		Reconnect,  // Twitch no longer takes the tokens, or they lack a scope
	};
	// Why it is off or needs reconnecting.
	enum class Why {
		Nothing,
		Unreachable, // Twitch could not be reached
		Expired,     // the code ran out before it was entered
		Denied,      // the streamer said no at twitch.tv/activate
		Refused,     // Twitch would not go on, for another reason (in the log)
		Scope,       // the tokens lack a scope
		SignedOut,   // Twitch no longer takes the tokens
	};
	// Called once, always later than the request, on the UI thread: Twitch's status and body, or status 0 and
	// {"message": why}.
	using Done = std::function<void(int status, const QByteArray &body)>;

	static const QStringList kScopes;

	explicit TwitchApi(Setup setup, QObject *parent = nullptr);

	State state() const { return state_; }
	Why why() const { return why_; }
	const QString &login() const { return login_; }
	const QString &userCode() const { return userCode_; } // while Waiting

	// OBS has finished loading: checks the remembered token, then every hour.
	void start();
	// Connect, or Reconnect: starts the device code flow, and opens twitch.tv/activate.
	void connectTwitch();
	// Opens twitch.tv/activate again, with the code filled in.
	void openActivation();
	// Forgets the tokens, and tells Twitch to forget them too.
	void disconnectTwitch();

	// A request to Helix as the broadcaster (sarxina_twitch_request): `path` starts after /helix and may have a query.
	void request(const QString &method, const QString &path, const QByteArray &body, Done done);

	// OBS is closing: nothing more is sent, and no Done is called again.
	void shutDown();

signals:
	void stateChanged();

private:
	enum class Refreshed { Yes, SignedOut, Unreachable };

	void send(const QString &method, const QString &path, const QByteArray &body, Done done, bool mayRefresh);
	void answerLater(Done done, const char *key);
	void refresh(const QString &usedToken, std::function<void(Refreshed)> then);
	void validate();
	void poll(quint64 flow);
	void pollLater(quint64 flow);
	void endFlow(Why why);
	bool readTokens(const QByteArray &body);
	bool hasScopes() const;
	void signOut(Why why);
	void forgetTokens();
	QByteArray form(const QList<std::pair<QString, QString>> &fields) const;
	void post(const QString &url, const QByteArray &form, TwitchHttp::Done done);
	void setState(State state, Why why = Why::Nothing);
	void load();
	void save() const;

	Setup setup_;
	TwitchHttp http_;
	State state_ = State::Off;
	Why why_ = Why::Nothing;
	bool closed_ = false;

	// The connection, remembered in twitch.json
	QString access_, refresh_;
	QString login_, userId_;
	QStringList scopes_;

	// Refreshing, and the requests waiting for it
	bool refreshing_ = false;
	std::vector<std::function<void(Refreshed)>> afterRefresh_;
	QTimer hourly_;

	// The device code flow. Every Connect starts a new one; answers for an older one are dropped.
	quint64 flow_ = 0;
	QString deviceCode_, userCode_;
	QUrl activation_;
	int interval_ = 5;  // Twitch's seconds between asking
	qint64 expires_ = 0; // ms after flowClock_ started
	QElapsedTimer flowClock_;
};

} // namespace sarxina
