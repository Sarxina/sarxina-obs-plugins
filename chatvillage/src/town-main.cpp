// The town without OBS, for working on the game: its page, trigger and feed on 127.0.0.1:4790 (town-server.hpp), the
// same way the plugin serves them, only with no chat. chatvillage\town.ps1 starts it and opens the page.
//
//   chatvillage-town <the game's folder>
#include "town-server.hpp"
#include "town.hpp"

#include <QCoreApplication>

#include <cstdio>

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <the game's folder>\n", argv[0]);
		return 2;
	}
	sarxina::chatvillage::Town town;
	sarxina::chatvillage::TownServer server(town);
	server.setPage(QString::fromLocal8Bit(argv[1]));
	if (!server.start()) {
		std::fprintf(stderr, "The town can't open on port %d: %s\n", sarxina::chatvillage::kPort,
			     server.error().toLocal8Bit().constData());
		return 1;
	}
	std::printf("The town is open at http://127.0.0.1:%d/ until this window closes.\n", sarxina::chatvillage::kPort);
	std::fflush(stdout);
	return app.exec();
}
