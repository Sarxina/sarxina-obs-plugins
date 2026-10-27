#include "profiles.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace sarxina::chatsaga {

int firstClassFor(const QString &userId)
{
	const QByteArray hash = QCryptographicHash::hash(userId.toUtf8(), QCryptographicHash::Sha1);
	return static_cast<unsigned char>(hash[0]) % kClassCount + 1;
}

int classNamed(const QString &name)
{
	// The game's Classes.json, in order.
	static const char *const names[kClassCount] = {"swordsman", "sorcerer",       "priest", "knight",
						       "martialartist", "magicswordsman", "hunter", "bandit"};
	QString letters;
	for (const QChar c : name.toLower())
		if (c.isLetter())
			letters += c;
	for (int i = 0; i < kClassCount; ++i)
		if (letters == QLatin1String(names[i]))
			return i + 1;
	return 0;
}

Profiles::Profiles(const QString &file) : file_(file)
{
	load();
}

int Profiles::classFor(const QString &userId)
{
	auto [at, isNew] = classes_.try_emplace(userId, 0);
	if (isNew) {
		at->second = firstClassFor(userId);
		save();
	}
	return at->second;
}

void Profiles::setClass(const QString &userId, int classId)
{
	if (classId < 1 || classId > kClassCount || classes_[userId] == classId)
		return;
	classes_[userId] = classId;
	save();
}

// {"profiles": {"<user id>": {"classId": 3}, ...}}; a class out of range is picked again.
void Profiles::load()
{
	QFile file(file_);
	if (file_.isEmpty() || !file.open(QIODevice::ReadOnly))
		return;
	const QJsonObject profiles = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("profiles")).toObject();
	for (auto at = profiles.begin(); at != profiles.end(); ++at) {
		const int classId = at.value().toObject().value(QStringLiteral("classId")).toInt();
		if (classId >= 1 && classId <= kClassCount)
			classes_[at.key()] = classId;
	}
}

void Profiles::save() const
{
	if (file_.isEmpty())
		return;
	QJsonObject profiles;
	for (const auto &[userId, classId] : classes_)
		profiles.insert(userId, QJsonObject{{QStringLiteral("classId"), classId}});
	QDir().mkpath(QFileInfo(file_).absolutePath());
	QSaveFile file(file_);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{{QStringLiteral("profiles"), profiles}}).toJson());
		file.commit();
	}
}

} // namespace sarxina::chatsaga
