// A small HTTP server on this computer only (127.0.0.1), for ChatVillage's town page, its local trigger and its feed.
// Each request is answered once, with JSON or a file, and the connection closed; or the connection is kept open as an
// event stream (text/event-stream) for events sent later.
//
// A request addressed to any host but 127.0.0.1 or localhost on its port is refused, so that no web page can reach it
// under a name of its own. Any page may read the answers to GET, which is how the game, loaded from a file, reads its
// feed; a page cannot send PUT or DELETE, since the server never allows them across origins.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;

namespace sarxina::chatvillage {

struct HttpRequest {
	QString method; // "GET", "PUT", ...
	QString path;   // without the query, still URL-encoded
	QByteArray body;
};

struct HttpAnswer {
	int status = 200;
	QJsonObject body;
	// With a name, the connection stays open as an event stream, and `body` is its first event.
	QString event;
	// With a type, the answer is this file instead of `body`.
	QByteArray file;
	QByteArray type;
};

class LocalHttp : public QObject {
public:
	using Route = std::function<HttpAnswer(const HttpRequest &)>;

	explicit LocalHttp(Route route, QObject *parent = nullptr);
	~LocalHttp() override;

	// Starts answering on the port. False when it cannot, with error() saying why.
	bool listen(quint16 port);
	void close();
	bool listening() const;
	QString error() const;

	// Sends an event to every open event stream.
	void broadcast(const QString &event, const QJsonObject &data);
	int streams() const { return static_cast<int>(streams_.size()); }

private:
	void accept();
	void read(QTcpSocket *socket);
	void refuse(QTcpSocket *socket, const QString &method, int status, const char *why);
	bool addressedHere(const QByteArray &host) const;
	void respond(QTcpSocket *socket, const HttpRequest &request, bool here);
	void openStream(QTcpSocket *socket, const HttpAnswer &answer);
	void forget(QTcpSocket *socket);

	Route route_;
	QTcpServer *server_;
	QHash<QTcpSocket *, QByteArray> reading_; // what came so far of each request not yet answered
	QSet<QTcpSocket *> streams_;              // the open event streams
};

} // namespace sarxina::chatvillage
