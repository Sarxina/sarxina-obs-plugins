// One websocket connection, kept open until it is closed: Twitch chat and VTube Studio both speak over one. It runs
// on a thread of its own so that OBS never waits on the network, and hands what arrives to the UI thread one message
// at a time. When a connection drops or cannot be made it tries again by itself, after 1, 2, 4 and then every
// 8 seconds. Made with libcurl, which OBS ships.
//
// A server may split a message into several frames. The libcurl that OBS 32.2 ships (8.12.1) does not say which frame
// is a message's last, so the owner says when what has arrived is a whole message.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace sarxina {

class WebSocket : public QObject {
	Q_OBJECT

public:
	// Whether what has arrived so far is a whole message.
	using Whole = bool (*)(const std::string &message);
	static bool wholeJson(const std::string &message);  // a JSON object or array, closed
	static bool wholeFrame(const std::string &message); // whatever a frame brought: Twitch never splits its lines

	explicit WebSocket(Whole whole, QObject *parent = nullptr);
	~WebSocket() override;

	// Connects to a ws:// or wss:// address, after closing any connection there was.
	void open(const QString &url);
	void close();
	// Sends one text message; dropped unless the connection is open.
	void send(const QByteArray &text);

signals:
	void opened();
	void received(const QByteArray &text);
	// The connection dropped, or a try to make it failed. It tries again by itself.
	void closed();

private:
	struct Attempt;

	void run();
	bool connectAndRead(const std::string &url, unsigned generation, bool sayWhy);
	void read(void *curl, unsigned generation);
	bool sendAll(void *curl, const std::string &text, unsigned generation);
	bool wanted(unsigned generation);
	bool take(unsigned generation, std::string &text);
	template<typename Call> void post(unsigned generation, Call call);

	const Whole whole_; // before the thread, which uses it

	// Shared with the thread, under mutex_. Every open() and close() starts a new generation; anything the thread is
	// still doing for an older one is dropped.
	std::mutex mutex_;
	std::condition_variable wake_;
	std::string url_;
	unsigned generation_ = 0;
	bool quit_ = false;
	std::deque<std::string> outbox_;

	bool open_ = false; // UI thread only
	std::thread thread_;
};

} // namespace sarxina
