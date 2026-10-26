#include "chat-presence.hpp"

#include <algorithm>

namespace sarxina::chatsaga {

const QStringList kBots{
	QStringLiteral("streamelements"), QStringLiteral("nightbot"),      QStringLiteral("moobot"),
	QStringLiteral("streamlabs"),     QStringLiteral("fossabot"),      QStringLiteral("wizebot"),
	QStringLiteral("soundalerts"),    QStringLiteral("sery_bot"),      QStringLiteral("botrixoficial"),
	QStringLiteral("kofistreambot"),  QStringLiteral("streamstickers"), QStringLiteral("tangiabot"),
	QStringLiteral("blerp"),          QStringLiteral("pokemoncommunitygame"),
};

ChatPresence::ChatPresence(double startedMs) : startedMs_(startedMs) {}

void ChatPresence::setBroadcaster(const QString &login)
{
	broadcaster_ = login.toLower();
}

void ChatPresence::setLiveSince(std::optional<double> ms)
{
	liveSinceMs_ = ms;
}

void ChatPresence::setListed(std::optional<std::vector<Chatter>> chatters)
{
	if (!chatters) {
		listed_.reset();
		return;
	}
	listed_.emplace();
	for (const Chatter &chatter : *chatters) {
		remember(chatter);
		listed_->insert(chatter.login.toLower());
	}
}

void ChatPresence::heard(const Chatter &chatter, const QString &text, double atMs)
{
	remember(chatter);
	Seen &seen = seen_[chatter.login.toLower()];
	if (atMs < seen.spokeMs)
		return; // older than what they said last
	seen.spokeMs = atMs;
	seen.lastSaid = text;
}

std::vector<Npc> ChatPresence::npcs(double nowMs) const
{
	std::vector<Npc> npcs;
	for (const QString &login : order_) {
		const QString id = npcId(login);
		if (!id.isEmpty() && isViewer(login) && isHere(login, nowMs))
			npcs.push_back({id, npcName(seen_.at(login).name, id), dialogueFor(login), spokeAtFor(login)});
	}
	return npcs;
}

QString ChatPresence::userIdOf(const QString &login) const
{
	const auto found = seen_.find(login.toLower());
	return found == seen_.end() ? QString() : found->second.userId;
}

// Not the broadcaster, and not a bot.
bool ChatPresence::isViewer(const QString &login) const
{
	return login != broadcaster_ && !kBots.contains(login);
}

// In Twitch's list, or chatted a moment ago.
bool ChatPresence::isHere(const QString &login, double nowMs) const
{
	if (listed_ && listed_->count(login))
		return true;
	const double spoke = seen_.at(login).spokeMs;
	return spoke >= 0 && nowMs - spoke <= kStillHereMs;
}

double ChatPresence::streamStartMs() const
{
	return liveSinceMs_.value_or(startedMs_);
}

// What they last said, if it was this stream; kLurking if they have said nothing since it began.
// When they last said something this stream; 0 if they have not.
qint64 ChatPresence::spokeAtFor(const QString &login) const
{
	const Seen &seen = seen_.at(login);
	return seen.spokeMs >= streamStartMs() ? static_cast<qint64>(seen.spokeMs) : 0;
}

QString ChatPresence::dialogueFor(const QString &login) const
{
	const Seen &seen = seen_.at(login);
	return seen.spokeMs >= streamStartMs() ? npcDialogue(seen.lastSaid) : kLurking;
}

// Keeps who they are, and their name as chat shows it now.
void ChatPresence::remember(const Chatter &chatter)
{
	const QString login = chatter.login.toLower();
	auto [at, isNew] = seen_.try_emplace(login);
	if (isNew)
		order_.push_back(login);
	if (!chatter.name.isEmpty())
		at->second.name = chatter.name;
	if (!chatter.userId.isEmpty())
		at->second.userId = chatter.userId;
}

} // namespace sarxina::chatsaga
