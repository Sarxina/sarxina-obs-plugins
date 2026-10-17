#include "catalog.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace sarxina {

namespace {

constexpr const char *kManagerId = "sarxina-obs-plugin-manager";

// Plain text for a label: no control characters, and not too long.
bool plainText(const QString &text, qsizetype longest)
{
	if (text.isEmpty() || text.size() > longest)
		return false;
	for (const QChar c : text)
		if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format)
			return false;
	return true;
}

// What may follow `downloads` in a zip's address: the release's tag and the file, or for a test server only the file.
// No "..", no %, no query: nothing that could lead the address anywhere else.
bool safeRest(const QString &rest, bool local)
{
	static const QRegularExpression github(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._-]*/[A-Za-z0-9_][A-Za-z0-9._-]*\\.zip$"));
	static const QRegularExpression test(
		QStringLiteral("^([A-Za-z0-9_][A-Za-z0-9._-]*/)?[A-Za-z0-9_][A-Za-z0-9._-]*\\.zip$"));
	return !rest.contains(QStringLiteral("..")) && (local ? test : github).match(rest).hasMatch();
}

bool readVersion(const QString &version, int (&parts)[3])
{
	static const QRegularExpression pattern(QStringLiteral("^(\\d{1,5})\\.(\\d{1,5})\\.(\\d{1,5})$"));
	const QRegularExpressionMatch match = pattern.match(version);
	if (!match.hasMatch())
		return false;
	for (int i = 0; i < 3; i++)
		parts[i] = match.captured(i + 1).toInt();
	return true;
}

} // namespace

Source Source::github()
{
	return {QStringLiteral("https://raw.githubusercontent.com/Sarxina/sarxina-obs-plugins/main/plugins.json"),
		QStringLiteral("https://github.com/Sarxina/sarxina-obs-plugins/releases/download/"), false};
}

std::optional<Source> Source::testServer(const QString &address)
{
	const QUrl url(address, QUrl::StrictMode);
	const QString host = url.host();
	if (!url.isValid() || url.scheme() != QLatin1String("http") || url.port() <= 0 || !url.userInfo().isEmpty() ||
	    url.hasQuery() || url.hasFragment() || (!url.path().isEmpty() && url.path() != QLatin1String("/")) ||
	    (host != QLatin1String("127.0.0.1") && host != QLatin1String("localhost") && host != QLatin1String("::1")))
		return std::nullopt;
	const QString base = QStringLiteral("http://%1:%2/")
				     .arg(host.contains(':') ? QStringLiteral("[%1]").arg(host) : host)
				     .arg(url.port());
	return Source{base + QStringLiteral("plugins.json"), base, true};
}

Source Source::forObs()
{
	const QString test = qEnvironmentVariable("SARXINA_OBS_PLUGIN_MANAGER_TEST_SERVER");
	if (!test.isEmpty())
		if (const std::optional<Source> local = testServer(test))
			return *local;
	return github();
}

bool validId(const QString &id)
{
	static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9-]{0,63}$"));
	return pattern.match(id).hasMatch();
}

bool newer(const QString &a, const QString &b)
{
	int x[3], y[3];
	if (!readVersion(a, x) || !readVersion(b, y))
		return false;
	for (int i = 0; i < 3; i++)
		if (x[i] != y[i])
			return x[i] > y[i];
	return false;
}

bool readIndex(const QByteArray &json, const Source &source, std::vector<Available> &list, QStringList &problems)
{
	list.clear();
	QJsonParseError error{};
	const QJsonDocument document = QJsonDocument::fromJson(json, &error);
	const QJsonValue plugins = document.object().value(QStringLiteral("plugins"));
	if (error.error != QJsonParseError::NoError || !plugins.isArray()) {
		problems << QStringLiteral("not a list of plugins");
		return false;
	}

	static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
	QSet<QString> seen;
	for (const QJsonValue &value : plugins.toArray()) {
		const QJsonObject entry = value.toObject();
		Available plugin;
		plugin.id = entry.value(QStringLiteral("id")).toString();
		plugin.name = entry.value(QStringLiteral("name")).toString();
		plugin.description = entry.value(QStringLiteral("description")).toString();
		plugin.version = entry.value(QStringLiteral("version")).toString();
		plugin.url = entry.value(QStringLiteral("url")).toString();
		plugin.sha256 = entry.value(QStringLiteral("sha256")).toString().toLatin1();

		int parts[3];
		const QString who = plugin.id.isEmpty() ? QStringLiteral("a plugin") : plugin.id.left(64);
		QString wrong;
		if (!validId(plugin.id) || plugin.id == QLatin1String(kManagerId))
			wrong = QStringLiteral("its id");
		else if (seen.contains(plugin.id))
			wrong = QStringLiteral("it is listed twice");
		else if (!plainText(plugin.name, 64) || !plainText(plugin.description, 500))
			wrong = QStringLiteral("its name or description");
		else if (!readVersion(plugin.version, parts))
			wrong = QStringLiteral("its version");
		else if (!plugin.url.startsWith(source.downloads) || !safeRest(plugin.url.mid(source.downloads.size()), source.local))
			wrong = QStringLiteral("its zip is not on Sarxina's releases");
		else if (!hex.match(QString::fromLatin1(plugin.sha256)).hasMatch())
			wrong = QStringLiteral("its SHA-256");
		if (!wrong.isEmpty()) {
			problems << QStringLiteral("%1 left out: %2").arg(who, wrong);
			continue;
		}
		seen.insert(plugin.id);
		list.push_back(std::move(plugin));
	}
	return true;
}

} // namespace sarxina
