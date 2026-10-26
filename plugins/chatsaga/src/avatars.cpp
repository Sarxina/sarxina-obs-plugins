#include "avatars.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>

namespace sarxina::chatsaga {

namespace {

constexpr size_t kUsersPerRequest = 100; // as many as Get Users takes at once

} // namespace

bool isTwitchPicture(const QUrl &url)
{
	return url.scheme() == QLatin1String("https") && url.host() == QLatin1String("static-cdn.jtvnw.net");
}

bool isFileSafeId(const QString &userId)
{
	static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
	return plain.match(userId).hasMatch();
}

Avatars::Avatars(const QString &folder, const QString &address, TwitchSend twitch, Download download)
	: folder_(folder), address_(address), twitch_(std::move(twitch)), download_(std::move(download))
{
	if (!download_) {
		network_ = new QNetworkAccessManager(this);
		network_->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
		download_ = [this](const QUrl &url, std::function<void(const QByteArray &)> done) {
			QNetworkReply *reply = network_->get(QNetworkRequest(url));
			connect(reply, &QNetworkReply::finished, this, [reply, done] {
				reply->deleteLater();
				const QByteArray body = reply->error() == QNetworkReply::NoError ? reply->read(kLongestAvatar + 1)
												   : QByteArray();
				done(body.size() > kLongestAvatar ? QByteArray() : body);
			});
		};
	}
}

void Avatars::want(const std::vector<QString> &userIds)
{
	std::vector<QString> fresh;
	for (const QString &userId : userIds)
		if (isFileSafeId(userId) && !asked_.contains(userId)) {
			asked_.insert(userId);
			fresh.push_back(userId);
		}
	for (size_t at = 0; at < fresh.size(); at += kUsersPerRequest)
		lookUp(std::vector<QString>(fresh.begin() + at, fresh.begin() + std::min(fresh.size(), at + kUsersPerRequest)));
}

QString Avatars::addressFor(const QString &userId) const
{
	return isFileSafeId(userId) && QFile::exists(fileFor(userId)) ? address_ + userId + QStringLiteral(".png") : QString();
}

// Asks Twitch for the users' pictures, and fetches each one it names.
void Avatars::lookUp(const std::vector<QString> &userIds)
{
	QStringList query;
	for (const QString &userId : userIds)
		query << QStringLiteral("id=") + userId;
	QPointer<Avatars> self = this;
	twitch_("GET", QStringLiteral("/users?") + query.join('&'), {}, [self](int status, const QJsonObject &answer) {
		if (!self || status != 200)
			return;
		for (const QJsonValue &user : answer.value(QStringLiteral("data")).toArray()) {
			const QJsonObject fields = user.toObject();
			const QUrl picture(fields.value(QStringLiteral("profile_image_url")).toString());
			const QString userId = fields.value(QStringLiteral("id")).toString();
			if (isFileSafeId(userId) && isTwitchPicture(picture))
				self->fetch(userId, picture);
		}
	});
}

void Avatars::fetch(const QString &userId, const QUrl &picture)
{
	QPointer<Avatars> self = this;
	download_(picture, [self, userId](const QByteArray &bytes) {
		if (self && !bytes.isEmpty())
			self->keep(userId, bytes);
	});
}

void Avatars::keep(const QString &userId, const QByteArray &picture)
{
	QDir().mkpath(folder_);
	QSaveFile file(fileFor(userId));
	if (!file.open(QIODevice::WriteOnly) || file.write(picture) != picture.size() || !file.commit())
		return;
	if (changed)
		changed();
}

QString Avatars::fileFor(const QString &userId) const
{
	return QDir(folder_).filePath(userId + QStringLiteral(".png"));
}

} // namespace sarxina::chatsaga
