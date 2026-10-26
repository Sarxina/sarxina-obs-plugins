#include "twitch-api.hpp"

#include <obs-module.h>
#include <util/util.hpp>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

#ifndef SARXINA_TWITCH_CLIENT_ID
#define SARXINA_TWITCH_CLIENT_ID ""
#endif

namespace sarxina {

namespace {

constexpr const char *kLog = "[Sarxina OBS Plugin Manager] Twitch:";
constexpr int kHour = 3600; // seconds between checks of the token, as Twitch wants

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// {"message": "..."}, the body plugins get when a request never reached Twitch.
QByteArray message(const QString &words)
{
	return QJsonDocument(QJsonObject{{QStringLiteral("message"), words}}).toJson(QJsonDocument::Compact);
}

// What Twitch said in an answer that is not what was asked for, for the log: its "message", cut short.
QByteArray said(const QByteArray &body)
{
	return QJsonDocument::fromJson(body).object().value(QStringLiteral("message")).toString().left(200).toUtf8();
}

bool matches(const char *pattern, const QString &text)
{
	return QRegularExpression(QString::fromLatin1(pattern)).match(text).hasMatch();
}

bool clientIdLike(const QString &id)
{
	return matches("^[A-Za-z0-9]{1,64}$", id);
}

// A token goes into a header, so it may hold nothing but printable ASCII without spaces.
bool tokenLike(const QString &token)
{
	return matches("^[\\x21-\\x7E]{1,512}$", token);
}

bool loginLike(const QString &login)
{
	return matches("^[A-Za-z0-9_]{1,25}$", login);
}

bool userIdLike(const QString &id)
{
	return matches("^[0-9]{1,20}$", id);
}

QStringList strings(const QJsonValue &array)
{
	QStringList list;
	for (const QJsonValue &value : array.toArray())
		list << value.toString();
	return list;
}

// A path into Helix as a plugin gives it: "/channel_points/custom_rewards?only_manageable_rewards=true". Nothing that
// could take the request anywhere else: no "..", no "#", no "\", no spaces, nothing but printable ASCII.
bool helixPath(const QString &path)
{
	if (!path.startsWith('/') || path.startsWith(QLatin1String("//")) || path.size() > 2000)
		return false;
	for (const QChar c : path)
		if (c.unicode() <= 0x20 || c.unicode() >= 0x7F || c == '#' || c == '\\')
			return false;
	const QStringList parts = path.section('?', 0, 0).split('/');
	return !parts.contains(QStringLiteral("..")) && !parts.contains(QStringLiteral("."));
}

// The path with broadcaster_id=<id> in its query, in place of any it had.
QString withBroadcaster(const QString &path, const QString &userId)
{
	const qsizetype question = path.indexOf('?');
	QStringList query;
	if (question >= 0)
		for (const QString &item : path.mid(question + 1).split('&', Qt::SkipEmptyParts))
			if (item.section('=', 0, 0) != QLatin1String("broadcaster_id"))
				query << item;
	query << QStringLiteral("broadcaster_id=") + userId;
	return (question >= 0 ? path.left(question) : path) + '?' + query.join('&');
}

// The page to enter the code on, as Twitch gave it with the code filled in, if it is on Twitch; else the plain one.
QUrl activationPage(const QString &given)
{
	const QUrl url(given, QUrl::StrictMode);
	const QString host = url.host().toLower();
	if (url.isValid() && url.scheme() == QLatin1String("https") && url.userInfo().isEmpty() && url.port() == -1 &&
	    (host == QLatin1String("www.twitch.tv") || host == QLatin1String("twitch.tv")))
		return url;
	return QUrl(QStringLiteral("https://www.twitch.tv/activate"));
}

const char *name(TwitchApi::State state)
{
	switch (state) {
	case TwitchApi::State::NoApp: return "this build has no Twitch app id";
	case TwitchApi::State::Off: return "not connected";
	case TwitchApi::State::Connecting: return "connecting";
	case TwitchApi::State::Waiting: return "waiting for the code to be entered at twitch.tv/activate";
	case TwitchApi::State::Ready: return "connected";
	case TwitchApi::State::Reconnect: return "needs reconnecting";
	}
	return "";
}

const char *name(TwitchApi::Why why)
{
	switch (why) {
	case TwitchApi::Why::Nothing: return "";
	case TwitchApi::Why::Unreachable: return " (Twitch cannot be reached)";
	case TwitchApi::Why::Expired: return " (the code ran out)";
	case TwitchApi::Why::Denied: return " (not allowed on Twitch)";
	case TwitchApi::Why::Refused: return " (Twitch would not go on)";
	case TwitchApi::Why::Scope: return " (the tokens lack a scope)";
	case TwitchApi::Why::SignedOut: return " (Twitch no longer takes the tokens)";
	}
	return "";
}

} // namespace

const QStringList TwitchApi::kScopes = {QStringLiteral("channel:read:redemptions"),
					QStringLiteral("channel:manage:redemptions"),
					QStringLiteral("moderator:read:chatters")};

TwitchApi::Setup TwitchApi::Setup::forObs()
{
	Setup setup;
	setup.hosts = TwitchHosts::forObs();
	setup.clientId = QStringLiteral(SARXINA_TWITCH_CLIENT_ID);
	const QString test = qEnvironmentVariable("SARXINA_OBS_PLUGIN_MANAGER_TWITCH_CLIENT_ID");
	if (clientIdLike(test))
		setup.clientId = test;
	BPtr<char> remembered(obs_module_config_path("twitch.json"));
	setup.remembered = QString::fromUtf8(remembered ? remembered.Get() : "");
	setup.open = [](const QUrl &url) { QDesktopServices::openUrl(url); };
	return setup;
}

TwitchApi::TwitchApi(Setup setup, QObject *parent) : QObject(parent), setup_(std::move(setup)), http_(setup_.hosts)
{
	if (!clientIdLike(setup_.clientId))
		setup_.clientId.clear();
	hourly_.setInterval(kHour * setup_.secondMs);
	connect(&hourly_, &QTimer::timeout, this, [this] {
		if (state_ == State::Ready)
			validate();
	});
	load();
	blog(LOG_INFO, "%s %s%s%s%s", kLog, name(state_), name(why_), state_ == State::Ready ? " as " : "",
	     state_ == State::Ready ? login_.toUtf8().constData() : "");
}

// ---- connecting -----------------------------------------------------------------------------------------------------

void TwitchApi::start()
{
	if (closed_ || state_ != State::Ready)
		return;
	hourly_.start();
	validate();
}

void TwitchApi::connectTwitch()
{
	if (closed_ || state_ == State::NoApp)
		return;
	const quint64 flow = ++flow_;
	forgetTokens();
	setState(State::Connecting);
	save();
	post(setup_.hosts.id + QStringLiteral("/oauth2/device"),
	     form({{QStringLiteral("client_id"), setup_.clientId}, {QStringLiteral("scopes"), kScopes.join(' ')}}),
	     [this, flow](int status, const QByteArray &body) {
		     if (closed_ || flow != flow_)
			     return;
		     const QJsonObject answer = QJsonDocument::fromJson(body).object();
		     const QString device = answer.value(QStringLiteral("device_code")).toString();
		     const QString user = answer.value(QStringLiteral("user_code")).toString();
		     if (status != 200 || !tokenLike(device) || !matches("^[A-Za-z0-9-]{1,32}$", user)) {
			     if (status != 0)
				     blog(LOG_WARNING, "%s Twitch gave no code: %d %s", kLog, status, said(body).constData());
			     endFlow(status == 0 ? Why::Unreachable : Why::Refused);
			     return;
		     }
		     deviceCode_ = device;
		     userCode_ = user;
		     activation_ = activationPage(answer.value(QStringLiteral("verification_uri")).toString());
		     interval_ = std::clamp(answer.value(QStringLiteral("interval")).toInt(5), 1, 60);
		     expires_ = static_cast<qint64>(std::clamp(answer.value(QStringLiteral("expires_in")).toInt(1800), 1, 3600)) *
				setup_.secondMs;
		     flowClock_.start();
		     setState(State::Waiting);
		     openActivation();
		     pollLater(flow);
	     });
}

void TwitchApi::openActivation()
{
	if (state_ == State::Waiting && setup_.open)
		setup_.open(activation_);
}

void TwitchApi::pollLater(quint64 flow)
{
	QTimer::singleShot(interval_ * setup_.secondMs, this, [this, flow] {
		if (!closed_ && flow == flow_ && state_ == State::Waiting)
			poll(flow);
	});
}

// Asks Twitch whether the code has been entered, and is given the tokens once it has.
void TwitchApi::poll(quint64 flow)
{
	post(setup_.hosts.id + QStringLiteral("/oauth2/token"),
	     form({{QStringLiteral("client_id"), setup_.clientId},
		   {QStringLiteral("scopes"), kScopes.join(' ')},
		   {QStringLiteral("device_code"), deviceCode_},
		   {QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")}}),
	     [this, flow](int status, const QByteArray &body) {
		     if (closed_ || flow != flow_ || state_ != State::Waiting)
			     return;
		     if (status == 200 && readTokens(body)) {
			     // Whose they are comes from checking the token.
			     deviceCode_.clear();
			     userCode_.clear();
			     setState(State::Connecting);
			     validate();
			     return;
		     }
		     const QString answer = QString::fromUtf8(said(body));
		     const bool late = flowClock_.elapsed() >= expires_;
		     if (answer == QLatin1String("slow_down"))
			     interval_ = std::min(interval_ + 5, 60);
		     // Not yet; or Twitch could not be reached this time, which is no reason to give up
		     if (!late && (status == 0 || status >= 500 || answer == QLatin1String("authorization_pending") ||
				   answer == QLatin1String("slow_down"))) {
			     pollLater(flow);
			     return;
		     }
		     Why why = Why::Refused;
		     if (answer.contains(QLatin1String("denied")) || answer.contains(QLatin1String("declined")))
			     why = Why::Denied;
		     else if (late || answer == QLatin1String("invalid device code") || answer == QLatin1String("expired_token"))
			     why = Why::Expired;
		     else
			     blog(LOG_WARNING, "%s Twitch did not give the tokens: %d %s", kLog, status, said(body).constData());
		     endFlow(why);
	     });
}

void TwitchApi::endFlow(Why why)
{
	flow_++;
	deviceCode_.clear();
	userCode_.clear();
	setState(State::Off, why);
	save();
}

void TwitchApi::disconnectTwitch()
{
	if (closed_ || state_ == State::NoApp)
		return;
	flow_++;
	// Twitch forgets the token too. Nothing waits for its answer.
	if (!access_.isEmpty())
		post(setup_.hosts.id + QStringLiteral("/oauth2/revoke"),
		     form({{QStringLiteral("client_id"), setup_.clientId}, {QStringLiteral("token"), access_}}),
		     [](int, const QByteArray &) {});
	deviceCode_.clear();
	userCode_.clear();
	forgetTokens();
	setState(State::Off);
	save();
}

// ---- the tokens -----------------------------------------------------------------------------------------------------

// Takes the tokens from Twitch's answer to the device code flow or to a refresh: {"access_token": ..., "refresh_token":
// ..., "scope": [...], ...}.
bool TwitchApi::readTokens(const QByteArray &body)
{
	const QJsonObject answer = QJsonDocument::fromJson(body).object();
	const QString access = answer.value(QStringLiteral("access_token")).toString();
	const QString refresh = answer.value(QStringLiteral("refresh_token")).toString();
	if (!tokenLike(access) || !tokenLike(refresh))
		return false;
	access_ = access;
	refresh_ = refresh;
	if (answer.value(QStringLiteral("scope")).isArray())
		scopes_ = strings(answer.value(QStringLiteral("scope")));
	return true;
}

bool TwitchApi::hasScopes() const
{
	return std::all_of(kScopes.begin(), kScopes.end(), [this](const QString &scope) { return scopes_.contains(scope); });
}

// Asks Twitch whose the token is and what it may do. Twitch wants this when the app starts and every hour.
void TwitchApi::validate()
{
	const QString token = access_;
	http_.send({"GET", setup_.hosts.id + QStringLiteral("/oauth2/validate"), {"Authorization: OAuth " + token.toUtf8()}, {}},
		   [this, token](int status, const QByteArray &body) {
			   if (closed_ || token.isEmpty() || token != access_)
				   return; // the tokens changed since
			   const QJsonObject answer = QJsonDocument::fromJson(body).object();
			   const QString login = answer.value(QStringLiteral("login")).toString();
			   const QString userId = answer.value(QStringLiteral("user_id")).toString();
			   if (status == 200 && loginLike(login) && userIdLike(userId)) {
				   login_ = login;
				   userId_ = userId;
				   scopes_ = strings(answer.value(QStringLiteral("scopes")));
				   if (hasScopes()) {
					   setState(State::Ready);
					   save();
				   } else {
					   signOut(Why::Scope);
				   }
				   return;
			   }
			   if (state_ == State::Connecting) {
				   // A new token, and Twitch will not say whose it is
				   if (status != 0)
					   blog(LOG_WARNING, "%s the new token did not check out: %d %s", kLog, status,
						said(body).constData());
				   forgetTokens();
				   setState(State::Off, status == 0 ? Why::Unreachable : Why::Refused);
				   save();
			   } else if (status == 401) {
				   refresh(token, [](Refreshed) {}); // and when that fails, it needs reconnecting
			   }
			   // Otherwise Twitch could not be reached: it is checked again in an hour.
		   });
}

// Gets a new access token with the refresh token, once for every request that was refused with the token it went
// out with. Public apps send no secret. The refresh token works only once, so the new one is kept at once.
void TwitchApi::refresh(const QString &usedToken, std::function<void(Refreshed)> then)
{
	if (state_ != State::Ready) {
		then(Refreshed::SignedOut);
		return;
	}
	if (usedToken != access_) {
		then(Refreshed::Yes); // refreshed already, since that request went out
		return;
	}
	afterRefresh_.push_back(std::move(then));
	if (refreshing_)
		return;
	refreshing_ = true;
	const QString used = refresh_;
	post(setup_.hosts.id + QStringLiteral("/oauth2/token"),
	     form({{QStringLiteral("client_id"), setup_.clientId},
		   {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
		   {QStringLiteral("refresh_token"), used}}),
	     [this, used](int status, const QByteArray &body) {
		     if (closed_ || used != refresh_)
			     return; // disconnected or connected anew since: whoever waited has been told
		     refreshing_ = false;
		     Refreshed refreshed = Refreshed::Yes;
		     if (status == 200 && readTokens(body)) {
			     blog(LOG_INFO, "%s the token was refreshed", kLog);
			     save();
			     if (!hasScopes())
				     signOut(Why::Scope);
		     } else if (status == 400 || status == 401) {
			     blog(LOG_WARNING, "%s Twitch no longer takes the refresh token: %d %s", kLog, status,
				  said(body).constData());
			     refreshed = Refreshed::SignedOut;
			     signOut(Why::SignedOut);
		     } else {
			     // Twitch could not be reached, or is too busy: the tokens stay for the next try.
			     if (status != 0)
				     blog(LOG_WARNING, "%s the token could not be refreshed now: %d", kLog, status);
			     refreshed = Refreshed::Unreachable;
		     }
		     std::vector<std::function<void(Refreshed)>> waiting;
		     waiting.swap(afterRefresh_);
		     for (const auto &next : waiting)
			     next(refreshed);
	     });
}

void TwitchApi::signOut(Why why)
{
	forgetTokens();
	setState(State::Reconnect, why);
	save();
}

void TwitchApi::forgetTokens()
{
	access_.clear();
	refresh_.clear();
	login_.clear();
	userId_.clear();
	scopes_.clear();
	refreshing_ = false;
	// Requests waiting for a refresh will not get one.
	std::vector<std::function<void(Refreshed)>> waiting;
	waiting.swap(afterRefresh_);
	for (const auto &next : waiting)
		next(Refreshed::SignedOut);
}

// ---- requests -------------------------------------------------------------------------------------------------------

void TwitchApi::request(const QString &method, const QString &path, const QByteArray &body, Done done)
{
	if (closed_ || !done)
		return;
	static const QStringList methods = {QStringLiteral("GET"), QStringLiteral("POST"), QStringLiteral("PATCH"),
					    QStringLiteral("DELETE")};
	if (!methods.contains(method) || !helixPath(path)) {
		blog(LOG_WARNING, "%s a plugin's request was not sent: it is not one Twitch's API takes", kLog);
		answerLater(std::move(done), "Api.NotARequest");
		return;
	}
	send(method, path, body, std::move(done), true);
}

void TwitchApi::send(const QString &method, const QString &path, const QByteArray &body, Done done, bool mayRefresh)
{
	switch (state_) {
	case State::NoApp: answerLater(std::move(done), "Api.NoApp"); return;
	case State::Reconnect: answerLater(std::move(done), "Api.Reconnect"); return;
	case State::Ready: break;
	default: answerLater(std::move(done), "Api.NotConnected"); return;
	}
	const QString token = access_;
	QList<QByteArray> headers = {"Client-Id: " + setup_.clientId.toUtf8(), "Authorization: Bearer " + token.toUtf8()};
	if (!body.isEmpty())
		headers << "Content-Type: application/json";
	http_.send({method.toUtf8(), setup_.hosts.api + withBroadcaster(path, userId_), headers, body},
		   [this, method, path, body, done, mayRefresh, token](int status, const QByteArray &answer) {
			   if (closed_)
				   return;
			   if (status == 401 && mayRefresh) {
				   refresh(token, [this, method, path, body, done](Refreshed refreshed) {
					   if (refreshed == Refreshed::Yes)
						   send(method, path, body, done, false);
					   else
						   done(0, message(text(refreshed == Refreshed::Unreachable ? "Api.Unreachable"
													    : "Api.Reconnect")));
				   });
				   return;
			   }
			   done(status, status == 0 ? message(text("Api.Unreachable")) : answer);
		   });
}

// Answers a request that will not be sent, later as every answer is, and never once OBS is closing.
void TwitchApi::answerLater(Done done, const char *key)
{
	QTimer::singleShot(0, this, [this, done = std::move(done), words = text(key)] {
		if (!closed_)
			done(0, message(words));
	});
}

void TwitchApi::shutDown()
{
	closed_ = true;
	flow_++;
	hourly_.stop();
	afterRefresh_.clear();
	http_.forget();
}

// ---- talking to id.twitch.tv ----------------------------------------------------------------------------------------

QByteArray TwitchApi::form(const QList<std::pair<QString, QString>> &fields) const
{
	QByteArray body;
	for (const auto &[key, value] : fields)
		body += (body.isEmpty() ? "" : "&") + key.toUtf8() + '=' + QUrl::toPercentEncoding(value);
	return body;
}

void TwitchApi::post(const QString &url, const QByteArray &form, TwitchHttp::Done done)
{
	http_.send({"POST", url, {"Content-Type: application/x-www-form-urlencoded"}, form}, std::move(done));
}

void TwitchApi::setState(State state, Why why)
{
	if (state == state_ && why == why_)
		return;
	state_ = state;
	why_ = why;
	if (state == State::Ready && !hourly_.isActive())
		hourly_.start();
	else if (state != State::Ready)
		hourly_.stop();
	blog(LOG_INFO, "%s %s%s%s%s", kLog, name(state), name(why), state == State::Ready ? " as " : "",
	     state == State::Ready ? login_.toUtf8().constData() : "");
	emit stateChanged();
}

// ---- remembering ----------------------------------------------------------------------------------------------------

void TwitchApi::load()
{
	if (setup_.clientId.isEmpty()) {
		state_ = State::NoApp;
		return;
	}
	if (setup_.remembered.isEmpty())
		return;
	QFile file(setup_.remembered);
	if (!file.open(QIODevice::ReadOnly))
		return;
	const QJsonObject remembered = QJsonDocument::fromJson(file.readAll()).object();
	const QString access = remembered.value(QStringLiteral("access_token")).toString();
	const QString refresh = remembered.value(QStringLiteral("refresh_token")).toString();
	const QString login = remembered.value(QStringLiteral("login")).toString();
	const QString userId = remembered.value(QStringLiteral("user_id")).toString();
	const QString reconnect = remembered.value(QStringLiteral("reconnect")).toString();
	if (tokenLike(access) && tokenLike(refresh) && loginLike(login) && userIdLike(userId)) {
		access_ = access;
		refresh_ = refresh;
		login_ = login;
		userId_ = userId;
		scopes_ = strings(remembered.value(QStringLiteral("scopes")));
		state_ = State::Ready;
		if (!hasScopes()) {
			forgetTokens();
			state_ = State::Reconnect;
			why_ = Why::Scope;
		}
	} else if (reconnect == QLatin1String("scope")) {
		state_ = State::Reconnect;
		why_ = Why::Scope;
	} else if (reconnect == QLatin1String("signed-out")) {
		state_ = State::Reconnect;
		why_ = Why::SignedOut;
	}
}

void TwitchApi::save() const
{
	if (setup_.remembered.isEmpty())
		return;
	QJsonObject remembered;
	if (!access_.isEmpty() && !userId_.isEmpty()) {
		remembered.insert(QStringLiteral("access_token"), access_);
		remembered.insert(QStringLiteral("refresh_token"), refresh_);
		remembered.insert(QStringLiteral("login"), login_);
		remembered.insert(QStringLiteral("user_id"), userId_);
		remembered.insert(QStringLiteral("scopes"), QJsonArray::fromStringList(scopes_));
	}
	if (state_ == State::Reconnect)
		remembered.insert(QStringLiteral("reconnect"),
				  why_ == Why::Scope ? QStringLiteral("scope") : QStringLiteral("signed-out"));

	QDir().mkpath(QFileInfo(setup_.remembered).absolutePath());
	QSaveFile file(setup_.remembered);
	if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(remembered).toJson()) < 0 || !file.commit())
		blog(LOG_WARNING, "%s cannot save %s", kLog, setup_.remembered.toUtf8().constData());
}

} // namespace sarxina
