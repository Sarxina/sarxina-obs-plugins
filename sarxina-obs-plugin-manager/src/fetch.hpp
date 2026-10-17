// Downloads the list of plugins and their zips, one at a time, on a thread of its own so that OBS never waits on the
// network. Made with libcurl, which OBS ships.
//
// From GitHub only HTTPS is spoken, and a redirect is followed only to GitHub itself or GitHub's download hosts
// (*.githubusercontent.com), five at most. From a test server on this computer: plain http, no redirects. Anything
// bigger than the limit it is given is cut off, and a download that stalls for 30 seconds is given up.
#pragma once

#include "catalog.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace sarxina {

class Fetcher : public QObject {
	Q_OBJECT

public:
	// Called once, on the UI thread: the body, or ok false and why not, in a few words.
	using Done = std::function<void(bool ok, const QByteArray &body, const QString &problem)>;

	explicit Fetcher(QObject *parent = nullptr);
	~Fetcher() override; // stops what is running; nothing more is called

	// Downloads `url`, which has to be the source's own (its list, or a zip on its releases).
	void get(const Source &source, const QString &url, qint64 limit, Done done);
	// Whether a redirect from GitHub may go to `url`.
	static bool githubHost(const QString &url);

private:
	struct Job {
		quint64 id;
		bool local;
		std::string url;
		qint64 limit;
	};
	struct Transfer;

	void run();
	void fetch(const Job &job);
	bool quitting();

	std::mutex mutex_;
	std::condition_variable wake_;
	std::deque<Job> jobs_;
	bool quit_ = false;

	quint64 next_ = 1;               // UI thread only
	std::map<quint64, Done> waiting_; // UI thread only
	std::thread thread_;
};

} // namespace sarxina
