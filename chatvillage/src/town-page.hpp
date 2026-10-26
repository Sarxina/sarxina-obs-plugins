// The town as a web page: the game's own files, handed to a browser from the game's folder, so that the town opens
// at http://127.0.0.1:4790/ in any browser on this computer (town-server.hpp).
#pragma once

#include "local-http.hpp"

#include <QString>

namespace sarxina::chatvillage {

// The answer to GET for this path (still URL-encoded): the file from the game's folder, with its type, "/" being the
// game's index.html; 404 for anything not in that folder.
HttpAnswer pageFile(const QString &folder, const QString &path);

} // namespace sarxina::chatvillage
