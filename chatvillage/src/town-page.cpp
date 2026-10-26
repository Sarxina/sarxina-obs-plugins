#include "town-page.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QUrl>

namespace sarxina::chatvillage {

namespace {

// The type a browser needs for each kind of file the game has.
QByteArray typeOf(const QString &file)
{
	static const QHash<QString, QByteArray> types{
		{QStringLiteral("html"), "text/html; charset=utf-8"},
		{QStringLiteral("js"), "text/javascript; charset=utf-8"},
		{QStringLiteral("css"), "text/css; charset=utf-8"},
		{QStringLiteral("json"), "application/json; charset=utf-8"},
		{QStringLiteral("png"), "image/png"},
		{QStringLiteral("ogg"), "audio/ogg"},
		{QStringLiteral("m4a"), "audio/mp4"},
		{QStringLiteral("woff"), "font/woff"},
		{QStringLiteral("woff2"), "font/woff2"},
		{QStringLiteral("ttf"), "font/ttf"},
		{QStringLiteral("wasm"), "application/wasm"},
	};
	return types.value(QFileInfo(file).suffix().toLower(), "application/octet-stream");
}

// The file the path names inside the folder, or "" when it names none there: nothing above the folder, no hidden
// tricks with separators or drive letters.
QString fileIn(const QString &folder, const QString &path)
{
	QString relative = QUrl::fromPercentEncoding(path.toUtf8()).mid(1);
	if (relative.isEmpty())
		relative = QStringLiteral("index.html");
	if (relative.contains('\\') || relative.contains(':') || relative.split('/').contains(QStringLiteral("..")))
		return {};
	const QString root = QFileInfo(folder).canonicalFilePath();
	const QFileInfo file(QDir(root).filePath(relative));
	const QString found = file.canonicalFilePath();
	return !root.isEmpty() && file.isFile() && found.startsWith(root + '/') ? found : QString();
}

} // namespace

HttpAnswer pageFile(const QString &folder, const QString &path)
{
	QFile file(fileIn(folder, path));
	if (file.fileName().isEmpty() || !file.open(QIODevice::ReadOnly))
		return {404, {{QStringLiteral("error"), QStringLiteral("not found")}}, {}, {}, {}};
	return {200, {}, {}, file.readAll(), typeOf(file.fileName())};
}

} // namespace sarxina::chatvillage
