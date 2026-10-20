// Text that comes from outside, a chatter's name, what they typed or the name of a hotkey, as a plugin shows it. It is
// only ever drawn as text, never read as markup; on top of that, the characters that turn text around or hide in it
// are taken out, the ends are trimmed, and it is cut to a length.
#pragma once

#include <QString>

namespace sarxina {

// At most `longest` characters, with "…" where it was cut.
QString plainText(const QString &text, int longest);

} // namespace sarxina
