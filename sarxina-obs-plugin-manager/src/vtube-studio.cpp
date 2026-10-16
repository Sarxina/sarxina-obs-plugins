#include "vtube-studio.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QJsonDocument>

namespace sarxina {

namespace {

constexpr qint64 kAnswerMs = 10000; // how long a request may wait for its answer
const QString kTokenRequest = QStringLiteral("sarxina-token");
const QString kLogIn = QStringLiteral("sarxina-login");

QJsonObject why(const QString &message)
{
	return {{QStringLiteral("message"), message}};
}

const char *name(VTubeStudio::State state)
{
	switch (state) {
	case VTubeStudio::State::Off: return "off";
	case VTubeStudio::State::Connecting: return "connecting";
	case VTubeStudio::State::Allowing: return "waiting for the streamer to allow the plugin in VTube Studio";
	case VTubeStudio::State::Ready: return "connected";
	case VTubeStudio::State::NotAllowed: return "not allowed in VTube Studio";
	case VTubeStudio::State::Retrying: return "cannot reach VTube Studio, trying again";
	}
	return "";
}

} // namespace

VTubeStudio::VTubeStudio(QObject *parent) : QObject(parent)
{
	connect(&socket_, &WebSocket::opened, this, &VTubeStudio::opened);
	connect(&socket_, &WebSocket::received, this, &VTubeStudio::received);
	connect(&socket_, &WebSocket::closed, this, &VTubeStudio::closed);
	expiry_.setInterval(1000);
	connect(&expiry_, &QTimer::timeout, this, &VTubeStudio::expire);
}

void VTubeStudio::connectTo(int port, const QString &token, bool mayAsk)
{
	failAll(QStringLiteral("the connection to VTube Studio was restarted"));
	port_ = port;
	token_ = token;
	mayAsk_ = mayAsk;
	setState(State::Connecting);
	socket_.open(QStringLiteral("ws://127.0.0.1:%1").arg(port));
}

void VTubeStudio::disconnect()
{
	socket_.close();
	failAll(QStringLiteral("not connected to VTube Studio"));
	setState(State::Off);
}

void VTubeStudio::request(const QString &type, const QJsonObject &data, Done done)
{
	if (state_ != State::Ready) {
		QTimer::singleShot(0, this, [done] { done(false, why(QStringLiteral("not connected to VTube Studio"))); });
		return;
	}
	const QString id = QString::number(++next_);
	pending_[id] = {std::move(done), QDateTime::currentMSecsSinceEpoch() + kAnswerMs};
	expiry_.start();
	send(type, data, id);
}

void VTubeStudio::send(const QString &type, const QJsonObject &data, const QString &id)
{
	QJsonObject message{
		{QStringLiteral("apiName"), QStringLiteral("VTubeStudioPublicAPI")},
		{QStringLiteral("apiVersion"), QStringLiteral("1.0")},
		{QStringLiteral("requestID"), id},
		{QStringLiteral("messageType"), type},
	};
	if (!data.isEmpty())
		message[QStringLiteral("data")] = data;
	socket_.send(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

// ---- logging in -----------------------------------------------------------------------------------------------------

void VTubeStudio::opened()
{
	if (!token_.isEmpty())
		logIn();
	else if (mayAsk_)
		askForToken();
	else
		refuse();
}

void VTubeStudio::logIn()
{
	setState(State::Connecting);
	send(QStringLiteral("AuthenticationRequest"),
	     {{QStringLiteral("pluginName"), kPluginName},
	      {QStringLiteral("pluginDeveloper"), kDeveloper},
	      {QStringLiteral("authenticationToken"), token_}},
	     kLogIn);
}

void VTubeStudio::askForToken()
{
	mayAsk_ = false; // one box per press of Connect
	setState(State::Allowing);
	send(QStringLiteral("AuthenticationTokenRequest"),
	     {{QStringLiteral("pluginName"), kPluginName}, {QStringLiteral("pluginDeveloper"), kDeveloper}},
	     kTokenRequest);
}

void VTubeStudio::refuse()
{
	socket_.close();
	failAll(QStringLiteral("not allowed in VTube Studio"));
	setState(State::NotAllowed);
}

void VTubeStudio::received(const QByteArray &text)
{
	const QJsonObject message = QJsonDocument::fromJson(text).object();
	const QString type = message.value(QStringLiteral("messageType")).toString();
	const QString id = message.value(QStringLiteral("requestID")).toString();
	const QJsonObject data = message.value(QStringLiteral("data")).toObject();

	if (id == kTokenRequest) {
		const QString token = data.value(QStringLiteral("authenticationToken")).toString();
		if (type != QStringLiteral("AuthenticationTokenResponse") || token.isEmpty()) {
			refuse(); // the streamer said no
			return;
		}
		token_ = token;
		emit tokenChanged(token_);
		logIn();
	} else if (id == kLogIn) {
		if (data.value(QStringLiteral("authenticated")).toBool()) {
			setState(State::Ready);
			return;
		}
		token_.clear(); // VTube Studio no longer takes it
		emit tokenChanged(token_);
		if (mayAsk_)
			askForToken();
		else
			refuse();
	} else if (const auto found = pending_.find(id); found != pending_.end()) {
		const Done done = std::move(found->second.done);
		pending_.erase(found);
		done(type != QStringLiteral("APIError"), data);
	}
}

// ---- losing it ------------------------------------------------------------------------------------------------------

void VTubeStudio::closed()
{
	failAll(QStringLiteral("lost the connection to VTube Studio"));
	setState(State::Retrying); // the socket tries again, and opened() logs in with the token
}

void VTubeStudio::failAll(const QString &message)
{
	std::map<QString, Pending> failed;
	failed.swap(pending_);
	expiry_.stop();
	for (auto &[id, pending] : failed)
		pending.done(false, why(message));
}

void VTubeStudio::expire()
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (auto it = pending_.begin(); it != pending_.end();) {
		if (it->second.deadline > now) {
			++it;
			continue;
		}
		const Done done = std::move(it->second.done);
		it = pending_.erase(it);
		done(false, why(QStringLiteral("VTube Studio did not answer in time")));
	}
	if (pending_.empty())
		expiry_.stop();
}

void VTubeStudio::setState(State state)
{
	if (state == state_)
		return;
	state_ = state;
	blog(LOG_INFO, "[Sarxina OBS Plugin Manager] VTube Studio (port %d): %s", port_, name(state));
	emit stateChanged();
}

} // namespace sarxina
