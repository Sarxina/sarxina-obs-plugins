#include "town-server.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>

namespace sarxina::chatsaga {

namespace {

const QString kNpcs = QStringLiteral("/npcs");
const QString kEvents = QStringLiteral("/npcs/events");
const QString kAvatars = QStringLiteral("/avatars/");

QJsonObject toJson(const Npc &npc)
{
	return {{QStringLiteral("id"), npc.id}, {QStringLiteral("name"), npc.name},
		{QStringLiteral("dialogue"), npc.dialogue}, {QStringLiteral("spokeAt"), static_cast<double>(npc.spokeAt)},
		{QStringLiteral("classId"), npc.classId}, {QStringLiteral("avatar"), npc.avatar}};
}

HttpAnswer refused(int status, const QString &why)
{
	return {status, {{QStringLiteral("error"), why}}, {}};
}

// The NPC's id in a path /npcs/<id>, or "" when the path names none that can be one.
QString idIn(const QString &path)
{
	const QString encoded = path.mid(kNpcs.size() + 1);
	if (encoded.isEmpty() || encoded.contains('/'))
		return {};
	return npcId(QUrl::fromPercentEncoding(encoded.toUtf8()));
}

// One text field of a PUT's body into `field`, if it is there. False when it is there but is not text.
bool textField(const QJsonObject &body, const char *key, std::optional<QString> &field)
{
	const QJsonValue given = body.value(QLatin1String(key));
	if (given.isUndefined())
		return true;
	if (!given.isString())
		return false;
	field = given.toString();
	return true;
}

// The time field of a PUT's body into `field`, if it is there. False when it is there but is not a number.
bool timeField(const QJsonObject &body, const char *key, std::optional<qint64> &field)
{
	const QJsonValue given = body.value(QLatin1String(key));
	if (given.isUndefined())
		return true;
	if (!given.isDouble())
		return false;
	field = static_cast<qint64>(given.toDouble());
	return true;
}

// A whole-number field of a PUT's body into `field`, if it is there. False when it is there but is not one.
bool numberField(const QJsonObject &body, const char *key, std::optional<int> &field)
{
	const QJsonValue given = body.value(QLatin1String(key));
	if (given.isUndefined())
		return true;
	if (!given.isDouble() || given.toDouble() != static_cast<double>(given.toInt()))
		return false;
	field = given.toInt();
	return true;
}

// What a PUT asks to change. False when the body is not one.
bool changeIn(const QByteArray &body, NpcChange &change)
{
	if (body.trimmed().isEmpty())
		return true;
	QJsonParseError error;
	const QJsonDocument json = QJsonDocument::fromJson(body, &error);
	if (error.error != QJsonParseError::NoError || !json.isObject())
		return false;
	const QJsonObject fields = json.object();
	return textField(fields, "name", change.name) && textField(fields, "dialogue", change.dialogue) &&
	       timeField(fields, "spokeAt", change.spokeAt) && numberField(fields, "classId", change.classId) &&
	       textField(fields, "avatar", change.avatar);
}

} // namespace

TownServer::TownServer(Town &town) : town_(town), http_([this](const HttpRequest &request) { return answer(request); })
{
	town_.shown = [this](const Npc &npc) { http_.broadcast(QStringLiteral("npc"), toJson(npc)); };
	town_.gone = [this](const QString &id) { http_.broadcast(QStringLiteral("gone"), {{QStringLiteral("id"), id}}); };
	town_.statusAsked = [this](const QString &id) {
		http_.broadcast(QStringLiteral("status"), {{QStringLiteral("id"), id}});
	};
}

bool TownServer::start(quint16 port)
{
	return http_.listen(port);
}

void TownServer::stop()
{
	http_.close();
}

HttpAnswer TownServer::answer(const HttpRequest &request)
{
	const bool get = request.method == QLatin1String("GET");
	if (request.path == kNpcs)
		return get ? list() : refused(405, QStringLiteral("GET"));
	if (request.path == kEvents)
		return get ? feed() : refused(405, QStringLiteral("GET"));
	if (request.path.startsWith(kAvatars))
		return get ? pageFile(avatars_, request.path.mid(kAvatars.size() - 1)) : refused(405, QStringLiteral("GET"));
	if (!request.path.startsWith(kNpcs + '/'))
		return get ? pageFile(page_, request.path) : refused(405, QStringLiteral("GET"));
	const QString id = idIn(request.path);
	if (id.isEmpty())
		return refused(400, QStringLiteral("an id is 1 to %1 characters").arg(kLongestId));
	if (request.method == QLatin1String("PUT"))
		return put(id, request.body);
	if (request.method == QLatin1String("DELETE"))
		return remove(id);
	return refused(405, QStringLiteral("PUT or DELETE"));
}

HttpAnswer TownServer::list() const
{
	QJsonArray npcs;
	for (const Npc &npc : town_.npcs())
		npcs.append(toJson(npc));
	return {200, {{QStringLiteral("npcs"), npcs}}, {}};
}

HttpAnswer TownServer::feed() const
{
	HttpAnswer everyone = list();
	everyone.event = QStringLiteral("town");
	return everyone;
}

HttpAnswer TownServer::put(const QString &id, const QByteArray &body)
{
	NpcChange change;
	if (!changeIn(body, change))
		return refused(400, QStringLiteral("the body has \"name\", \"dialogue\" and \"avatar\" as text, \"spokeAt\" and \"classId\" as numbers, or is empty"));
	const bool added = town_.put(id, change) == Town::Put::Added;
	QJsonObject answer = toJson(*town_.find(id));
	answer.insert(QStringLiteral("added"), added);
	return {added ? 201 : 200, answer, {}};
}

HttpAnswer TownServer::remove(const QString &id)
{
	const bool removed = town_.remove(id);
	return {removed ? 200 : 404, {{QStringLiteral("id"), id}, {QStringLiteral("removed"), removed}}, {}};
}

} // namespace sarxina::chatsaga
