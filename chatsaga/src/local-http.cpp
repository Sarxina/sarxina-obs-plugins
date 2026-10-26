#include "local-http.hpp"

#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>

namespace sarxina::chatsaga {

namespace {

constexpr int kMostHead = 8 * 1024; // bytes of a request's line and headers
constexpr int kMostBody = 4 * 1024; // bytes of its body

// A request's line and the headers that matter here.
struct Head {
	bool ok = false;
	QString method;
	QString path;
	QByteArray host;
	qsizetype length = 0; // of the body
};

// A header's number, or -1 when it is not one.
qsizetype number(const QByteArray &value)
{
	bool ok = false;
	const qlonglong read = value.toLongLong(&ok);
	return ok ? static_cast<qsizetype>(read) : -1;
}

Head readHead(const QByteArray &head)
{
	Head read;
	const QList<QByteArray> lines = head.split('\n');
	const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
	if (request.size() != 3 || !request[2].startsWith("HTTP/1."))
		return read;
	read.method = QString::fromLatin1(request[0]);
	read.path = QString::fromLatin1(request[1].split('?').value(0));
	for (qsizetype i = 1; i < lines.size(); i++) {
		const qsizetype colon = lines[i].indexOf(':');
		if (colon < 0)
			continue;
		const QByteArray name = lines[i].left(colon).trimmed().toLower();
		const QByteArray value = lines[i].mid(colon + 1).trimmed();
		if (name == "host")
			read.host = value.toLower();
		else if (name == "content-length")
			read.length = number(value);
	}
	read.ok = read.path.startsWith('/') && read.length >= 0;
	return read;
}

QByteArray reason(int status)
{
	switch (status) {
	case 200: return "OK";
	case 201: return "Created";
	case 400: return "Bad Request";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 413: return "Content Too Large";
	default: return "Error";
	}
}

QByteArray eventText(const QString &name, const QJsonObject &data)
{
	return "event: " + name.toUtf8() + "\ndata: " + QJsonDocument(data).toJson(QJsonDocument::Compact) + "\n\n";
}

// Writes a whole answer and closes the connection once it is sent. Any page may read an answer to GET.
void answerOnce(QTcpSocket *socket, const QString &method, int status, const QByteArray &type, const QByteArray &content)
{
	QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\nContent-Type: " + type +
			  "\r\nContent-Length: " + QByteArray::number(content.size()) +
			  "\r\nCache-Control: no-cache\r\nConnection: close\r\n";
	if (method == QLatin1String("GET"))
		head += "Access-Control-Allow-Origin: *\r\n";
	socket->write(head + "\r\n" + content);
	socket->disconnectFromHost();
}

void answerOnce(QTcpSocket *socket, const QString &method, int status, const QJsonObject &body)
{
	answerOnce(socket, method, status, "application/json; charset=utf-8",
		   QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QJsonObject problem(const char *why)
{
	return {{QStringLiteral("error"), QString::fromUtf8(why)}};
}

} // namespace

LocalHttp::LocalHttp(Route route, QObject *parent) : QObject(parent), route_(std::move(route)), server_(new QTcpServer(this))
{
	connect(server_, &QTcpServer::newConnection, this, &LocalHttp::accept);
}

LocalHttp::~LocalHttp()
{
	close();
}

bool LocalHttp::listen(quint16 port)
{
	return server_->isListening() || server_->listen(QHostAddress::LocalHost, port);
}

void LocalHttp::close()
{
	server_->close();
	const auto open = reading_.keys() + streams_.values();
	for (QTcpSocket *socket : open)
		socket->abort();
}

bool LocalHttp::listening() const
{
	return server_->isListening();
}

QString LocalHttp::error() const
{
	return server_->errorString();
}

void LocalHttp::broadcast(const QString &name, const QJsonObject &data)
{
	const QByteArray written = eventText(name, data);
	for (QTcpSocket *socket : std::as_const(streams_))
		socket->write(written);
}

void LocalHttp::accept()
{
	while (QTcpSocket *socket = server_->nextPendingConnection()) {
		reading_.insert(socket, {});
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] { forget(socket); });
	}
}

// Gathers a request until it is whole, then answers it.
void LocalHttp::read(QTcpSocket *socket)
{
	if (!reading_.contains(socket)) { // an event stream: nothing more is read from it
		socket->readAll();
		return;
	}
	QByteArray &got = reading_[socket];
	got += socket->readAll();
	const qsizetype end = got.indexOf("\r\n\r\n");
	if (end < 0) {
		if (got.size() > kMostHead)
			refuse(socket, {}, 413, "the request is too long");
		return;
	}
	const Head head = readHead(got.left(end));
	if (!head.ok)
		return refuse(socket, {}, 400, "not a request");
	if (head.length > kMostBody)
		return refuse(socket, head.method, 413, "the body is too long");
	if (got.size() < end + 4 + head.length)
		return; // more of the body to come
	const HttpRequest request{head.method, head.path, got.mid(end + 4, head.length)};
	reading_.remove(socket);
	respond(socket, request, addressedHere(head.host));
}

// Answers a request that cannot be taken, and reads no more of it.
void LocalHttp::refuse(QTcpSocket *socket, const QString &method, int status, const char *why)
{
	reading_.remove(socket);
	answerOnce(socket, method, status, problem(why));
}

bool LocalHttp::addressedHere(const QByteArray &host) const
{
	const QByteArray port = ':' + QByteArray::number(server_->serverPort());
	return host == "127.0.0.1" + port || host == "localhost" + port;
}

void LocalHttp::respond(QTcpSocket *socket, const HttpRequest &request, bool here)
{
	if (!here)
		return answerOnce(socket, request.method, 403, problem("only from 127.0.0.1 or localhost"));
	const HttpAnswer answer = route_(request);
	if (!answer.event.isEmpty() && answer.status == 200)
		return openStream(socket, answer);
	if (!answer.type.isEmpty())
		return answerOnce(socket, request.method, answer.status, answer.type, answer.file);
	answerOnce(socket, request.method, answer.status, answer.body);
}

void LocalHttp::openStream(QTcpSocket *socket, const HttpAnswer &answer)
{
	streams_.insert(socket);
	socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
		      "Connection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\n" +
		      eventText(answer.event, answer.body));
}

void LocalHttp::forget(QTcpSocket *socket)
{
	reading_.remove(socket);
	streams_.remove(socket);
	socket->deleteLater();
}

} // namespace sarxina::chatsaga
