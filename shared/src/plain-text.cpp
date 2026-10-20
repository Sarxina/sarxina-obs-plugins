#include "plain-text.hpp"

#include <QChar>
#include <QList>

namespace sarxina {

namespace {

// Control characters, the ones that change the direction of text (which can make a name look like something else),
// and the invisible ones.
bool hidden(char32_t c)
{
	return c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0x061C || (c >= 0x200B && c <= 0x200F) ||
	       (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2069) || c == 0xFEFF;
}

} // namespace

QString plainText(const QString &text, int longest)
{
	QString clean;
	for (const char32_t c : text.toUcs4()) {
		if (hidden(c))
			continue;
		clean += QString::fromUcs4(&c, 1);
	}
	clean = clean.simplified();

	const QList<uint> characters = clean.toUcs4();
	if (longest < 1 || characters.size() <= longest)
		return clean;
	QString cut;
	for (int i = 0; i < longest - 1; i++) {
		const char32_t c = characters[i];
		cut += QString::fromUcs4(&c, 1);
	}
	return cut.trimmed() + QChar(0x2026);
}

} // namespace sarxina
