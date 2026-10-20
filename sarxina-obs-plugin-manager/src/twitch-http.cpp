#include "twitch-http.hpp"

#include <curl/curl.h>
#include <obs-module.h>

#include <QUrl>

namespace sarxina {

namespace {

constexpr long kConnectSeconds = 15;
constexpr long kStalledSeconds = 30; // less than a byte a second for this long gives up

} // namespace

// What curl's callbacks need for one request.
struct TwitchHttp::Transfer {
	TwitchHttp *http;
	std::string body;
	bool tooBig = false;
};

TwitchHosts TwitchHosts::twitch()
{
	return {QStringLiteral("https://id.twitch.tv"), QStringLiteral("https://api.twitch.tv/helix"), false};
}

std::optional<TwitchHosts> TwitchHosts::testServer(const QString &address)
{
	const QUrl url(address, QUrl::StrictMode);
	if (!url.isValid() || url.scheme() != QLatin1String("http") || url.host() != QLatin1String("127.0.0.1") ||
	    url.port() <= 0 || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
	    (!url.path().isEmpty() && url.path() != QLatin1String("/")))
		return std::nullopt;
	const QString base = QStringLiteral("http://127.0.0.1:%1").arg(url.port());
	return TwitchHosts{base, base + QStringLiteral("/helix"), true};
}

TwitchHosts TwitchHosts::forObs()
{
	const QString test = qEnvironmentVariable("SARXINA_OBS_PLUGIN_MANAGER_TWITCH_TEST_SERVER");
	if (!test.isEmpty())
		if (const std::optional<TwitchHosts> local = testServer(test))
			return *local;
	return twitch();
}

TwitchHttp::TwitchHttp(TwitchHosts hosts, QObject *parent)
	: QObject(parent),
	  hosts_(std::move(hosts)),
	  thread_([this] { run(); })
{
}

TwitchHttp::~TwitchHttp()
{
	{
		std::lock_guard lock(mutex_);
		quit_ = true;
		jobs_.clear();
	}
	wake_.notify_all();
	thread_.join();
}

bool TwitchHttp::allowed(const TwitchHosts &hosts, const QString &address)
{
	const QUrl url(address, QUrl::StrictMode);
	const QString host = url.host();
	return (address.startsWith(hosts.id + '/') || address.startsWith(hosts.api + '/')) && url.isValid() &&
	       url.userInfo().isEmpty() && !url.hasFragment() &&
	       (hosts.local ? url.scheme() == QLatin1String("http") && host == QLatin1String("127.0.0.1")
			    : url.scheme() == QLatin1String("https") && url.port() == -1 &&
				      (host == QLatin1String("id.twitch.tv") || host == QLatin1String("api.twitch.tv")));
}

void TwitchHttp::send(Request request, Done done)
{
	const quint64 id = next_++;
	waiting_[id] = std::move(done);
	if (!allowed(hosts_, request.url)) {
		blog(LOG_WARNING, "[Sarxina OBS Plugin Manager] not sent: %s is not Twitch", request.url.toUtf8().constData());
		QMetaObject::invokeMethod(
			this,
			[this, id] {
				const auto found = waiting_.find(id);
				if (found == waiting_.end())
					return;
				const Done waiting = std::move(found->second);
				waiting_.erase(found);
				waiting(0, {});
			},
			Qt::QueuedConnection);
		return;
	}
	Job job{id, request.method.toStdString(), request.url.toStdString(), request.body.toStdString(), {}};
	for (const QByteArray &header : request.headers)
		job.headers.push_back(header.toStdString());
	{
		std::lock_guard lock(mutex_);
		jobs_.push_back(std::move(job));
	}
	wake_.notify_all();
}

void TwitchHttp::forget()
{
	waiting_.clear();
	std::lock_guard lock(mutex_);
	jobs_.clear();
}

bool TwitchHttp::quitting()
{
	std::lock_guard lock(mutex_);
	return quit_;
}

void TwitchHttp::run()
{
	std::unique_lock lock(mutex_);
	for (;;) {
		wake_.wait(lock, [this] { return quit_ || !jobs_.empty(); });
		if (quit_)
			return;
		const Job job = std::move(jobs_.front());
		jobs_.pop_front();
		lock.unlock();
		perform(job);
		lock.lock();
	}
}

void TwitchHttp::perform(const Job &job)
{
	CURL *curl = curl_easy_init();
	if (!curl)
		return;
	Transfer transfer{this, {}};
	curl_slist *headers = nullptr;
	for (const std::string &header : job.headers)
		headers = curl_slist_append(headers, header.c_str());
	headers = curl_slist_append(headers, "Expect:"); // the body goes at once, without asking first

	curl_easy_setopt(curl, CURLOPT_URL, job.url.c_str());
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, hosts_.local ? "http" : "https");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	if (hosts_.local)
		curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
	if (job.method == "GET") {
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	} else {
		if (job.method != "POST")
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, job.method.c_str());
		if (job.method == "POST" || !job.body.empty()) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(job.body.size()));
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, job.body.c_str());
		}
	}
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectSeconds);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kStalledSeconds);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Sarxina OBS Plugin Manager/" PLUGIN_VERSION);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *param) -> size_t {
		auto *transfer = static_cast<Transfer *>(param);
		const size_t bytes = size * count;
		if (static_cast<qint64>(transfer->body.size() + bytes) > kLargestAnswer) {
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
				 return static_cast<Transfer *>(param)->http->quitting() ? 1 : 0;
			 });

	const CURLcode result = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);

	if (result == CURLE_ABORTED_BY_CALLBACK)
		return; // OBS is closing
	if (result != CURLE_OK) {
		// The address without its query, and never a header: the tokens are in those.
		const std::string where = job.url.substr(0, job.url.find('?'));
		blog(LOG_INFO, "[Sarxina OBS Plugin Manager] cannot reach Twitch (%s %s): %s", job.method.c_str(),
		     where.c_str(), transfer.tooBig ? "the answer is too big" : curl_easy_strerror(result));
		status = 0;
		transfer.body.clear();
	}

	QMetaObject::invokeMethod(
		this,
		[this, id = job.id, status = static_cast<int>(status), body = QByteArray::fromStdString(transfer.body)] {
			const auto found = waiting_.find(id);
			if (found == waiting_.end())
				return;
			const Done done = std::move(found->second);
			waiting_.erase(found);
			done(status, body);
		},
		Qt::QueuedConnection);
}

} // namespace sarxina
