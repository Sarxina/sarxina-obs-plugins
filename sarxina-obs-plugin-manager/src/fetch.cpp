#include "fetch.hpp"

#include <curl/curl.h>
#include <obs-module.h>

#include <QUrl>

namespace sarxina {

namespace {

constexpr long kConnectSeconds = 15;
constexpr long kStalledSeconds = 30; // less than a byte a second for this long gives up
constexpr int kRedirects = 5;

} // namespace

// What curl's callbacks need for one request.
struct Fetcher::Transfer {
	Fetcher *fetcher;
	qint64 limit;
	std::string body;
	bool tooBig = false;
};

Fetcher::Fetcher(QObject *parent) : QObject(parent), thread_([this] { run(); }) {}

Fetcher::~Fetcher()
{
	{
		std::lock_guard lock(mutex_);
		quit_ = true;
		jobs_.clear();
	}
	wake_.notify_all();
	thread_.join();
}

bool Fetcher::githubHost(const QString &address)
{
	const QUrl url(address, QUrl::StrictMode);
	const QString host = url.host().toLower();
	return url.isValid() && url.scheme() == QLatin1String("https") && url.userInfo().isEmpty() &&
	       (url.port() == -1 || url.port() == 443) &&
	       (host == QLatin1String("github.com") || host.endsWith(QLatin1String(".githubusercontent.com")));
}

void Fetcher::get(const Source &source, const QString &url, qint64 limit, Done done)
{
	const quint64 id = next_++;
	waiting_[id] = std::move(done);
	{
		std::lock_guard lock(mutex_);
		jobs_.push_back({id, source.local, url.toStdString(), limit});
	}
	wake_.notify_all();
}

bool Fetcher::quitting()
{
	std::lock_guard lock(mutex_);
	return quit_;
}

void Fetcher::run()
{
	std::unique_lock lock(mutex_);
	for (;;) {
		wake_.wait(lock, [this] { return quit_ || !jobs_.empty(); });
		if (quit_)
			return;
		const Job job = jobs_.front();
		jobs_.pop_front();
		lock.unlock();
		fetch(job);
		lock.lock();
	}
}

void Fetcher::fetch(const Job &job)
{
	bool ok = false;
	QString problem;
	std::string body;
	std::string url = job.url;
	for (int hop = 0;; hop++) {
		CURL *curl = curl_easy_init();
		if (!curl) {
			problem = QStringLiteral("libcurl did not start");
			break;
		}
		Transfer transfer{this, job.limit};
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, job.local ? "http" : "https");
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // redirects are looked at below, one by one
		if (job.local)
			curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectSeconds);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kStalledSeconds);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "Sarxina OBS Plugin Manager/" PLUGIN_VERSION);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *param) -> size_t {
			auto *transfer = static_cast<Transfer *>(param);
			const size_t bytes = size * count;
			if (static_cast<qint64>(transfer->body.size() + bytes) > transfer->limit) {
				transfer->tooBig = true;
				return 0; // anything short of `bytes` stops it
			}
			transfer->body.append(data, bytes);
			return bytes;
		});
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
				 +[](void *param, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
					 return static_cast<Transfer *>(param)->fetcher->quitting() ? 1 : 0;
				 });

		const CURLcode result = curl_easy_perform(curl);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		const char *location = nullptr;
		curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location);
		const std::string redirect = location ? location : "";
		curl_easy_cleanup(curl);

		if (result == CURLE_ABORTED_BY_CALLBACK)
			return; // OBS is closing
		if (result != CURLE_OK) {
			problem = transfer.tooBig ? QStringLiteral("it is too big") : QString::fromUtf8(curl_easy_strerror(result));
			break;
		}
		if (status >= 300 && status < 400 && !redirect.empty()) {
			if (job.local || hop >= kRedirects || !githubHost(QString::fromStdString(redirect))) {
				problem = QStringLiteral("it was sent somewhere other than GitHub");
				break;
			}
			url = redirect;
			continue;
		}
		if (status != 200) {
			problem = QStringLiteral("the server answered %1").arg(status);
			break;
		}
		ok = true;
		body = std::move(transfer.body);
		break;
	}
	if (!ok)
		blog(LOG_INFO, "[Sarxina OBS Plugin Manager] cannot download %s: %s", job.url.c_str(),
		     problem.toUtf8().constData());

	QMetaObject::invokeMethod(
		this,
		[this, id = job.id, ok, body = QByteArray::fromStdString(body), problem] {
			const auto found = waiting_.find(id);
			if (found == waiting_.end())
				return;
			const Done done = std::move(found->second);
			waiting_.erase(found);
			done(ok, body, problem);
		},
		Qt::QueuedConnection);
}

} // namespace sarxina
