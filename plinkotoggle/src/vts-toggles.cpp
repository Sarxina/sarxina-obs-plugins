#include "vts-toggles.hpp"

#include "plain-text.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace sarxina::plinkotoggle {

namespace {

constexpr int kLongestName = 64; // characters; a slot shortens it further to what fits

// A hotkey with no name of its own goes by its file ("Glasses.exp3.json" is "Glasses"), or else by what it does.
QString nameOf(const QJsonObject &hotkey)
{
	QString name = plainText(hotkey.value(QStringLiteral("name")).toString(), kLongestName);
	if (!name.isEmpty())
		return name;
	QString file = hotkey.value(QStringLiteral("file")).toString();
	file = file.mid(file.lastIndexOf(QLatin1Char('/')) + 1);
	file = file.mid(file.lastIndexOf(QLatin1Char('\\')) + 1);
	name = plainText(file.left(file.indexOf(QLatin1Char('.'))), kLongestName);
	if (!name.isEmpty())
		return name;
	return plainText(hotkey.value(QStringLiteral("type")).toString(), kLongestName);
}

} // namespace

Model readHotkeys(bool ok, const char *json)
{
	Model model;
	const QJsonObject data = QJsonDocument::fromJson(QByteArray(json ? json : "")).object();
	if (!ok) {
		model.state = Model::State::NotConnected;
		return model;
	}
	if (!data.value(QStringLiteral("modelLoaded")).toBool()) {
		model.state = Model::State::NoModel;
		return model;
	}
	model.state = Model::State::Loaded;
	model.id = data.value(QStringLiteral("modelID")).toString().toStdString();
	model.name = plainText(data.value(QStringLiteral("modelName")).toString(), kLongestName);
	for (const QJsonValue &value : data.value(QStringLiteral("availableHotkeys")).toArray()) {
		const QJsonObject hotkey = value.toObject();
		Toggle toggle;
		toggle.id = hotkey.value(QStringLiteral("hotkeyID")).toString().toStdString();
		toggle.type = hotkey.value(QStringLiteral("type")).toString().toStdString();
		toggle.file = hotkey.value(QStringLiteral("file")).toString().toStdString();
		toggle.name = nameOf(hotkey);
		if (toggle.id.empty() || toggle.type == "Unset")
			continue;
		model.toggles.push_back(std::move(toggle));
	}
	return model;
}

} // namespace sarxina::plinkotoggle
