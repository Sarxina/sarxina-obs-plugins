#include "websocket.hpp"

#include <curl/curl.h>
#include <obs-module.h>
#include <winsock2.h>

#include <algorithm>
#include <chrono>

namespace sarxina {

namespace {

constexpr long kConnectSeconds = 10;          // to reach the server and finish the handshake
constexpr size_t kLargestMessage = 8u << 20; // a server sending more than this in one message is cut off
constexpr long kWaitMicroseconds = 20000;     // how long the thread waits on the socket before it looks for work

// Waits until the socket can be read from (or written to), or a short while has passed.
void waitFor(curl_socket_t socket, bool write)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(socket, &set);
	timeval wait{0, kWaitMicroseconds};
	select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &wait);
}

} // namespace

// What curl's progress callback needs to stop a connection attempt that is no longer wanted.
struct WebSocket::Attempt {
	WebSocket *socket;
	unsigned generation;
};

WebSocket::WebSocket(QObject *parent) : QObject(parent), thread_([this] { run(); }) {}

WebSocket::~WebSocket()
{
	{
		std::lock_guard lock(mutex_);
		quit_ = true;
		generation_++;
	}
	wake_.notify_all();
	thread_.join();
}

void WebSocket::open(const QString &url)
{
	{
		std::lock_guard lock(mutex_);
		url_ = url.toStdString();
		generation_++;
		outbox_.clear();
	}
	open_ = false;
	wake_.notify_all();
}

void WebSocket::close()
{
	{
		std::lock_guard lock(mutex_);
		url_.clear();
		generation_++;
		outbox_.clear();
	}
	open_ = false;
	wake_.notify_all();
}

void WebSocket::send(const QByteArray &text)
{
	if (!open_)
		return;
	std::lock_guard lock(mutex_);
	outbox_.emplace_back(text.toStdString());
}

// Runs `call` on the UI thread, unless the connection it is about has been closed or replaced by then.
template<typename Call> void WebSocket::post(unsigned generation, Call call)
{
	QMetaObject::invokeMethod(
		this,
		[this, generation, call] {
			if (generation == generation_) // written only on this thread
				call();
		},
		Qt::QueuedConnection);
}

bool WebSocket::wanted(unsigned generation)
{
	std::lock_guard lock(mutex_);
	return generation == generation_ && !quit_;
}

bool WebSocket::take(unsigned generation, std::string &text)
{
	std::lock_guard lock(mutex_);
	if (generation != generation_ || outbox_.empty())
		return false;
	text = std::move(outbox_.front());
	outbox_.pop_front();
	return true;
}

// ---- the thread -----------------------------------------------------------------------------------------------------

void WebSocket::run()
{
	int failures = 0; // tries in a row that could not connect
	unsigned last = 0;
	std::unique_lock lock(mutex_);
	for (;;) {
		wake_.wait(lock, [this] { return quit_ || !url_.empty(); });
		if (quit_)
			return;
		const std::string url = url_;
		const unsigned generation = generation_;
		if (generation != last)
			failures = 0;
		last = generation;

		lock.unlock();
		const bool connected = connectAndRead(url, generation, failures == 0);
		lock.lock();
		if (quit_)
			return;
		if (generation != generation_)
			continue; // opened elsewhere or closed meanwhile: no pause

		failures = connected ? 0 : failures + 1;
		const int seconds = connected ? 1 : 1 << std::min(failures - 1, 3);
		wake_.wait_for(lock, std::chrono::seconds(seconds),
			       [this, generation] { return quit_ || generation != generation_; });
	}
}

// One connection, from the first try to connect until it ends. True if it got connected.
bool WebSocket::connectAndRead(const std::string &url, unsigned generation, bool sayWhy)
{
	CURL *curl = curl_easy_init();
	if (!curl)
		return false;

	Attempt attempt{this, generation};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L); // 2: connect, do the websocket handshake, and hand it over
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectSeconds);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, kConnectSeconds);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &attempt);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
			 +[](void *data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
				 const auto *attempt = static_cast<const Attempt *>(data);
				 return attempt->socket->wanted(attempt->generation) ? 0 : 1; // non-zero stops it
			 });

	const CURLcode result = curl_easy_perform(curl);
	const bool connected = result == CURLE_OK;
	if (connected) {
		post(generation, [this] {
			open_ = true;
			emit opened();
		});
		read(curl, generation);
	} else if (sayWhy && result != CURLE_ABORTED_BY_CALLBACK) { // once, not on every try
		blog(LOG_INFO, "[Sarxina OBS Plugin Manager] cannot connect to %s: %s", url.c_str(),
		     curl_easy_strerror(result));
	}
	curl_easy_cleanup(curl);

	post(generation, [this] {
		open_ = false;
		emit closed();
	});
	return connected;
}

// Sends what there is to send and reads what arrives, until the connection ends or is not wanted any more.
void WebSocket::read(void *handle, unsigned generation)
{
	CURL *curl = handle;
	curl_socket_t socket = CURL_SOCKET_BAD;
	if (curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket) != CURLE_OK || socket == CURL_SOCKET_BAD)
		return;

	std::string message;
	char buffer[16384];
	while (wanted(generation)) {
		for (std::string text; take(generation, text);)
			if (!sendAll(curl, text, generation))
				return;

		waitFor(socket, false);
		// Read until there is nothing left, also what curl may already hold decoded.
		for (;;) {
			size_t got = 0;
			const curl_ws_frame *frame = nullptr;
			const CURLcode result = curl_ws_recv(curl, buffer, sizeof(buffer), &got, &frame);
			if (result == CURLE_AGAIN)
				break;
			if (result != CURLE_OK || !frame || (frame->flags & CURLWS_CLOSE))
				return;
			if (!(frame->flags & (CURLWS_TEXT | CURLWS_BINARY)))
				continue; // a ping, which curl answers itself
			message.append(buffer, got);
			if (message.size() > kLargestMessage)
				return;
			if (frame->bytesleft == 0 && !(frame->flags & CURLWS_CONT)) {
				post(generation, [this, text = QByteArray::fromStdString(message)] { emit received(text); });
				message.clear();
			}
		}
	}
}

bool WebSocket::sendAll(void *handle, const std::string &text, unsigned generation)
{
	CURL *curl = handle;
	curl_socket_t socket = CURL_SOCKET_BAD;
	curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket);
	size_t done = 0;
	while (done < text.size()) {
		size_t sent = 0;
		const CURLcode result = curl_ws_send(curl, text.data() + done, text.size() - done, &sent, 0, CURLWS_TEXT);
		if (result != CURLE_OK && result != CURLE_AGAIN)
			return false;
		done += sent;
		if (done < text.size()) {
			if (!wanted(generation))
				return false;
			waitFor(socket, true);
		}
	}
	return true;
}

} // namespace sarxina
