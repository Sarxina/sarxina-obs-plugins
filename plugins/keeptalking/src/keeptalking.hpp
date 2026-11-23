// KeepTalking on OBS's UI thread, as a plugin of the Sarxina OBS Plugin Manager: its settings (drawn behind the
// manager's gear, kept in its own config.json), the mic's level, and the countdown.
//
// The mic's level comes from an OBS volume meter on the audio source picked, the same level OBS's mixer shows (after
// the source's filters and volume, so a muted mic is silent). Every 50 ms the countdown (silence-countdown.hpp) is
// stepped: it is armed while the plugin is on, the mic is found and the game is running. At zero the game's exe is
// closed (processes.hpp). The number goes to every countdown source (countdown-source.hpp). KeepTalking does all of
// this only while OBS streams: off stream there is no countdown and no count.
//
// The same mic's sound (after its filters, before its volume; nothing while it is muted) also goes to a word counter
// (word-counter.hpp): this stream's words, from 0 at Start Streaming, held from Stop Streaming until the next stream,
// shown in the settings and in every word count source (word-count-source.hpp), and logged with the stream's length when
// it ends. A stream of 5 minutes or more is then sent to the KeepTalking leaderboard (leaderboard.hpp), unless that
// is turned off in the settings.
#pragma once

#include "leaderboard.hpp"
#include "silence-countdown.hpp"
#include "word-counter.hpp"

#include <obs.hpp>

#include <QDateTime>
#include <QLineEdit>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <string>

namespace sarxina::keeptalking {

// What the test route's "status" answers, from any thread.
struct Status {
	bool on = false;
	std::string game;
	bool gameRunning = false;
	std::string mic;
	bool micFound = false;
	double levelDb = -1e9;
	double thresholdDb = 0;
	int seconds = 0;
	int shown = -1;
	bool streaming = false;
	long long words = 0;      // this stream's, or the last stream's once it ended
	double streamSeconds = 0; // how long that stream has been (or was) live
	bool leaderboardSent = false; // the last stream sent to the leaderboard, or not
	std::string leaderboard;      // what came of it, as the settings show it ("Sent: Long Stream, rank 3")
};

class KeepTalking : public QObject {
public:
	KeepTalking();
	~KeepTalking() override;

	// Called once, from obs_module_post_load: hands the plugin to the manager, or runs it alone.
	void start();
	void shutDown(); // OBS is closing
	// The scene collection loaded or changed: the mic may only exist now.
	void findMic();
	// OBS started or stopped streaming: KeepTalking works only in between.
	void streamStarted();
	void streamStopped();

	// What the manager calls.
	void setEnabled(bool enabled);
	obs_properties_t *properties();
	void getSettings(obs_data_t *settings) const;
	void update(obs_data_t *settings);

	// The test route.
	void startCountdown(); // as if the quiet had lasted long enough
	void talk();           // as if the mic went over the level
	// Sends a stream to the leaderboard now, as if it had just ended (whatever the setting and its length).
	void submitTest(long long words, double seconds, const QDateTime &started, const QString &login = {});
	Status status() const;

private:
	void tick();
	void slowTick();
	void attachMic();
	void detachMic();
	void showLevel();
	void closeGame();
	void load();
	void save() const;
	static void levelArrived(void *param, const float magnitude[], const float peak[], const float inputPeak[]);
	static void soundArrived(void *param, obs_source_t *source, const struct audio_data *audio, bool muted);
	void showWords();
	void submit(const LeaderboardStream &stream);
	void showResult(const LeaderboardResult &result);
	long long words() const;
	double liveSeconds() const; // how long this stream has been live, or the last one was

	// The settings.
	bool on_ = false;
	std::string game_;
	std::string mic_;
	double thresholdDb_ = -40;
	int seconds_ = 5;
	bool leaderboard_ = true; // send each stream to the leaderboard

	bool managed_ = false;
	bool enabled_ = false;
	bool closed_ = false;
	bool gameRunning_ = false;

	obs_volmeter_t *meter_ = nullptr;
	OBSWeakSourceAutoRelease attached_;
	// Written on OBS's audio thread.
	std::atomic<float> levelDb_{-1e9f};
	std::atomic<float> threshold_{-40};
	std::atomic<uint64_t> lastLoudNs_{0};

	// The word count. The counter lives on OBS's audio thread and counts while counting_ (on, and streaming); this
	// stream's words are its syllables since the stream started.
	WordCounter counter_;
	std::atomic<bool> counting_{false};
	std::atomic<uint64_t> syllables_{0}; // the counter's, for the UI thread
	bool streaming_ = false;
	uint64_t streamStartSyllables_ = 0, streamStartNs_ = 0, streamEndNs_ = 0;
	QPointer<QLineEdit> wordsField_; // the count in the manager's settings, while they are open
	QDateTime streamStartedAt_;      // Start Streaming, UTC, for the leaderboard

	// The leaderboard: the last stream's result (read by status() from any thread, so under statusLock_).
	QNetworkAccessManager *network_ = nullptr;
	LeaderboardResult result_;
	QPointer<QLineEdit> resultField_; // the result in the manager's settings, while they are open

	SilenceCountdown countdown_;
	int shown_ = -1;
	QTimer tick_, slow_;
	QPointer<QLineEdit> levelField_; // the level in the manager's settings, while they are open

	mutable std::mutex statusLock_;
	Status status_;
};

} // namespace sarxina::keeptalking
