#include "leaderboard.hpp"

#include "sarxina-obs-plugin-manager.h"

#include <obs-module.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>

#include <memory>
#include <string>
#include <thread>

#include <windows.h>
#include <winhttp.h>

namespace sarxina::keeptalking {

namespace {

// The leaderboard service (a Lambda function URL): its host; requests go to https://<host>/submit.
constexpr const wchar_t *kLeaderboardHost = L"ozubde2aiomsocqghzp4zvxuxa0yocei.lambda-url.us-east-1.on.aws";
constexpr int kTimeoutMs = 20000;

// HTTPS through Windows' own WinHTTP: OBS ships Qt Network without a TLS backend, so Qt cannot reach https.
struct HttpAnswer {
	int status = 0;
	QByteArray body;
	QString problem; // set when the request never got an answer
};

QString windowsProblem(const char *step)
{
	return QStringLiteral("%1 failed (Windows error %2)").arg(QString::fromLatin1(step)).arg(GetLastError());
}

HttpAnswer postJson(const wchar_t *host, const wchar_t *path, const QByteArray &json)
{
	HttpAnswer out;
	const std::wstring agent = L"KeepTalking/" + QString::fromLatin1(PLUGIN_VERSION).toStdWString();
	HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) {
		out.problem = windowsProblem("WinHttpOpen");
		return out;
	}
	WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
	HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER,
							   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
				       : nullptr;
	const wchar_t *headers = L"Content-Type: application/json\r\n";
	if (!request) {
		out.problem = windowsProblem(connection ? "WinHttpOpenRequest" : "WinHttpConnect");
	} else if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), const_cast<char *>(json.constData()),
				       static_cast<DWORD>(json.size()), static_cast<DWORD>(json.size()), 0) ||
		   !WinHttpReceiveResponse(request, nullptr)) {
		out.problem = windowsProblem("sending");
	} else {
		DWORD status = 0, size = sizeof(status);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				    &status, &size, WINHTTP_NO_HEADER_INDEX);
		out.status = static_cast<int>(status);
		for (;;) {
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
				break;
			QByteArray chunk(static_cast<int>(available), Qt::Uninitialized);
			DWORD read = 0;
			if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
				break;
			out.body.append(chunk.constData(), static_cast<int>(read));
		}
	}
	if (request)
		WinHttpCloseHandle(request);
	if (connection)
		WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return out;
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QByteArray utf8(const QString &words)
{
	return words.toUtf8();
}

QString categoryName(const QString &key)
{
	if (key == QLatin1String("speedrun"))
		return text("Leaderboard.Speedrun");
	if (key == QLatin1String("short"))
		return text("Leaderboard.Short");
	if (key == QLatin1String("long"))
		return text("Leaderboard.Long");
	if (key == QLatin1String("marathon"))
		return text("Leaderboard.Marathon");
	return key;
}

using Done = std::function<void(const LeaderboardResult &)>;

void post(const LeaderboardStream &stream, const QJsonObject &channel, QPointer<QNetworkAccessManager> network,
	  Done done)
{
	if (!network)
		return;
	QJsonObject body;
	body["channel_id"] = channel["broadcaster_id"].toString();
	body["login"] = channel["broadcaster_login"].toString();
	body["display"] = channel["broadcaster_name"].toString();
	body["words"] = static_cast<qint64>(stream.words);
	body["seconds"] = stream.seconds;
	body["started"] = stream.started.toUTC().toString(Qt::ISODate);
	body["version"] = QStringLiteral(PLUGIN_VERSION);

	blog(LOG_INFO, "[%s] leaderboard: sending %lld words in %.0f s as %s", PLUGIN_TITLE, stream.words,
	     stream.seconds, utf8(body["login"].toString()).constData());
	const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
	// The request runs on its own thread; its answer is handled back on the UI thread, while the plugin lives.
	std::thread([json, network, done] {
		const HttpAnswer reply = postJson(kLeaderboardHost, L"/submit", json);
		if (!network)
			return;
		QMetaObject::invokeMethod(network.data(), [reply, done] {
		const int status = reply.status;
		QJsonParseError parsed{};
		const QJsonDocument answerDoc = QJsonDocument::fromJson(reply.body, &parsed);
		const QJsonObject answer = answerDoc.object();
		if (parsed.error == QJsonParseError::NoError && answer.contains("ok")) {
			if (answer["ok"].toBool()) {
				const QString category = categoryName(answer["category"].toString());
				const int rank = answer["rank"].toInt();
				const bool best = answer["best"].toBool();
				blog(LOG_INFO, "[%s] leaderboard: sent: %s, rank %d%s", PLUGIN_TITLE,
				     utf8(category).constData(), rank, best ? " (this channel's best there)" : "");
				done({true, text("Leaderboard.Sent").arg(category).arg(rank)});
			} else {
				const QString error = answer["error"].toString();
				blog(LOG_WARNING, "[%s] leaderboard: refused (HTTP %d): %s", PLUGIN_TITLE, status,
				     utf8(error).constData());
				done({false, notSent(error.isEmpty() ? text("Leaderboard.Refused") : error)});
			}
			return;
		}
		const QString problem = !reply.problem.isEmpty()
						? reply.problem
						: QStringLiteral("HTTP %1, an answer that is not the leaderboard's").arg(status);
		blog(LOG_WARNING, "[%s] leaderboard: could not reach it: %s", PLUGIN_TITLE, utf8(problem).constData());
		done({false, notSent(text("Leaderboard.Unreachable").arg(problem))});
		}, Qt::QueuedConnection);
	}).detach();
}
} // namespace

QString notSent(const QString &reason)
{
	return text("Leaderboard.NotSent").arg(reason);
}

void submitAsChannel(const LeaderboardStream &stream, const QString &login, QNetworkAccessManager *network, Done done)
{
	QJsonObject channel;
	channel["broadcaster_id"] = QStringLiteral("test-") + login;
	channel["broadcaster_login"] = login;
	channel["broadcaster_name"] = login;
	post(stream, channel, network, std::move(done));
}

void submitToLeaderboard(const LeaderboardStream &stream, QNetworkAccessManager *network, Done done)
{
	// The channel first, over the manager's Twitch connection; its answer comes on the UI thread. As
	// shared/src/chat-trigger.cpp's sendToTwitch does it.
	struct Pending {
		LeaderboardStream stream;
		QPointer<QNetworkAccessManager> network;
		Done done;
	};
	auto *pending = new Pending{stream, network, std::move(done)};
	const sarxina_twitch_request request{
		"GET", "/channels", nullptr,
		[](void *param, int status, const char *body) {
			std::unique_ptr<Pending> waiting(static_cast<Pending *>(param));
			if (!waiting->network)
				return; // the plugin is gone
			const QJsonObject answer = QJsonDocument::fromJson(body ? body : "{}").object();
			const QString message = answer["message"].toString();
			if (status == 0) {
				blog(LOG_WARNING, "[%s] leaderboard: not sent, not connected to Twitch: %s", PLUGIN_TITLE,
				     utf8(message).constData());
				waiting->done({false, notSent(text("Leaderboard.NoTwitch"))});
				return;
			}
			if (status != 200) {
				blog(LOG_WARNING, "[%s] leaderboard: not sent, Twitch answered %d: %s", PLUGIN_TITLE, status,
				     utf8(message).constData());
				waiting->done({false, notSent(text("Leaderboard.Twitch").arg(message))});
				return;
			}
			const QJsonObject channel = answer["data"].toArray().at(0).toObject();
			if (channel["broadcaster_id"].toString().isEmpty() ||
			    channel["broadcaster_login"].toString().isEmpty()) {
				blog(LOG_WARNING, "[%s] leaderboard: not sent, Twitch did not name the channel",
				     PLUGIN_TITLE);
				waiting->done({false, notSent(text("Leaderboard.NoChannel"))});
				return;
			}
			post(waiting->stream, channel, waiting->network, waiting->done);
		},
		pending};
	if (!obs_initialized() || !sarxina_obs_plugin_manager_twitch_request(&request)) {
		Done failed = std::move(pending->done);
		delete pending;
		blog(LOG_WARNING,
		     "[%s] leaderboard: not sent, the Sarxina OBS Plugin Manager (with Twitch) is not installed",
		     PLUGIN_TITLE);
		failed({false, notSent(text("Leaderboard.NoManager"))});
	}
}

} // namespace sarxina::keeptalking
