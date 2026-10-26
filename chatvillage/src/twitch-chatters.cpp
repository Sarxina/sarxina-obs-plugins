#include "twitch-chatters.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QPointer>
#include <QUrl>

namespace sarxina::chatvillage {

namespace {

QJsonObject first(const QJsonObject &answer)
{
	return answer.value(QStringLiteral("data")).toArray().first().toObject();
}

QString text(const QJsonObject &object, const char *key)
{
	return object.value(QLatin1String(key)).toString();
}

QString encoded(const QString &value)
{
	return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

} // namespace

TwitchChatters::TwitchChatters(TwitchSend send) : send_(std::move(send))
{
	connect(&timer_, &QTimer::timeout, this, &TwitchChatters::poll);
}

void TwitchChatters::start(int everyMs)
{
	timer_.start(everyMs);
	poll();
}

void TwitchChatters::stop()
{
	timer_.stop();
}

// One round: the broadcaster, the first time; then the stream; then chat, page by page.
void TwitchChatters::poll()
{
	if (asking_)
		return;
	asking_ = true;
	if (broadcasterId_.isEmpty())
		askChannel();
	else
		askStream();
}

void TwitchChatters::askChannel()
{
	ask(QStringLiteral("/channels"), [this](const QJsonObject &answer) {
		const QJsonObject channel = first(answer);
		broadcasterId_ = text(channel, "broadcaster_id");
		if (broadcaster)
			broadcaster(text(channel, "broadcaster_login"));
		askStream();
	});
}

void TwitchChatters::askStream()
{
	ask(QStringLiteral("/streams?user_id=") + encoded(broadcasterId_), [this](const QJsonObject &answer) {
		const QDateTime since = QDateTime::fromString(text(first(answer), "started_at"), Qt::ISODate);
		if (live)
			live(since.isValid() ? std::optional<double>(since.toMSecsSinceEpoch()) : std::nullopt);
		askChatters({}, {});
	});
}

// Twitch gives up to 1000 chatters a page, and a cursor for the next.
void TwitchChatters::askChatters(const QString &after, std::vector<Chatter> sofar)
{
	QString path = QStringLiteral("/chat/chatters?first=1000&moderator_id=") + encoded(broadcasterId_);
	if (!after.isEmpty())
		path += QStringLiteral("&after=") + encoded(after);
	ask(path, [this, sofar = std::move(sofar)](const QJsonObject &answer) mutable {
		for (const QJsonValue &chatter : answer.value(QStringLiteral("data")).toArray())
			sofar.push_back({text(chatter.toObject(), "user_login"), text(chatter.toObject(), "user_name")});
		const QString next =
			answer.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("cursor")).toString();
		if (!next.isEmpty())
			return askChatters(next, std::move(sofar));
		asking_ = false;
		setProblem({});
		if (listed)
			listed(std::move(sofar));
	});
}

// Asks Twitch, and goes on with its answer if it is one to go on with.
void TwitchChatters::ask(const QString &path, std::function<void(const QJsonObject &)> then)
{
	QPointer<TwitchChatters> self = this;
	const bool sent = send_("GET", path, {}, [self, then = std::move(then)](int status, const QJsonObject &answer) {
		if (self && self->answered(status, answer))
			then(answer);
	});
	if (!sent)
		answered(0, {{QStringLiteral("message"),
			      QStringLiteral("Needs the Sarxina OBS Plugin Manager 1.2.0 or newer, with Twitch connected.")}});
}

// True for an answer to go on with. Otherwise the round ends, and there is no list until Twitch answers again.
bool TwitchChatters::answered(int status, const QJsonObject &answer)
{
	if (status == 200)
		return true;
	asking_ = false;
	const QString why = text(answer, "message");
	setProblem(why.isEmpty() ? QStringLiteral("Twitch answered %1.").arg(status) : why);
	if (listed)
		listed(std::nullopt);
	return false;
}

void TwitchChatters::setProblem(const QString &problem)
{
	if (problem == problem_)
		return;
	problem_ = problem;
	if (problemChanged)
		problemChanged();
}

} // namespace sarxina::chatvillage
