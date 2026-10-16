// The one connection to VTube Studio's public API that every plugin shares. The manager logs in as the plugin
// "Sarxina OBS Plugin Manager" by Sarxina, and plugins send their requests through it.
//
// VTube Studio asks the streamer once, in a box inside VTube Studio, whether to allow a new plugin. It then gives a
// token that is remembered, so later connections need no box. The box only ever comes up after Connect is pressed in
// the manager's dock, never on its own.
#pragma once

#include "websocket.hpp"

#include <QJsonObject>
#include <QTimer>

#include <functional>
#include <map>

namespace sarxina {

class VTubeStudio : public QObject {
	Q_OBJECT

public:
	enum class State {
		Off,
		Connecting,
		Allowing,   // VTube Studio is showing the box that asks the streamer to allow the manager
		Ready,      // logged in
		NotAllowed, // the streamer said no, or took the permission back
		Retrying,   // VTube Studio cannot be reached (not running, or its API is off)
	};
	// Called once, always later than the request: with the answer's data, or ok false and {"message": why}.
	using Done = std::function<void(bool ok, const QJsonObject &data)>;

	static constexpr const char *kPluginName = "Sarxina OBS Plugin Manager";
	static constexpr const char *kDeveloper = "Sarxina";

	explicit VTubeStudio(QObject *parent = nullptr);

	// Connects to VTube Studio on this computer and logs in with `token`. With no token, or one VTube Studio no longer
	// takes, it asks for a new one only if `mayAsk`, and only once.
	void connectTo(int port, const QString &token, bool mayAsk);
	void disconnect();
	State state() const { return state_; }
	int port() const { return port_; }

	void request(const QString &type, const QJsonObject &data, Done done);

signals:
	void stateChanged();
	void tokenChanged(const QString &token);

private:
	struct Pending {
		Done done;
		qint64 deadline; // ms since epoch
	};

	void opened();
	void received(const QByteArray &text);
	void closed();
	void send(const QString &type, const QJsonObject &data, const QString &id);
	void logIn();
	void askForToken();
	void refuse(); // the streamer has to allow it: stop until Connect is pressed again
	void failAll(const QString &why);
	void expire();
	void setState(State state);

	WebSocket socket_;
	State state_ = State::Off;
	int port_ = 8001;
	QString token_;
	bool mayAsk_ = false;
	std::map<QString, Pending> pending_;
	quint64 next_ = 0;
	QTimer expiry_;
};

} // namespace sarxina
