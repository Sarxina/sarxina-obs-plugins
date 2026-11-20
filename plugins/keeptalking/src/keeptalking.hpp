// KeepTalking on OBS's UI thread, as a plugin of the Sarxina OBS Plugin Manager: its settings (drawn behind the
// manager's gear, kept in its own config.json), the mic's level, and the countdown.
//
// The mic's level comes from an OBS volume meter on the audio source picked, the same level OBS's mixer shows (after
// the source's filters and volume, so a muted mic is silent). Every 50 ms the countdown (silence-countdown.hpp) is
// stepped: it is armed while the plugin is on, the mic is found and the game is running. At zero the game's exe is
// closed (processes.hpp). The number goes to every countdown source (countdown-source.hpp).
#pragma once

#include "silence-countdown.hpp"

#include <obs.hpp>

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

	// What the manager calls.
	void setEnabled(bool enabled);
	obs_properties_t *properties();
	void getSettings(obs_data_t *settings) const;
	void update(obs_data_t *settings);

	// The test route.
	void startCountdown(); // as if the quiet had lasted long enough
	void talk();           // as if the mic went over the level
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

	// The settings.
	bool on_ = false;
	std::string game_;
	std::string mic_;
	double thresholdDb_ = -40;
	int seconds_ = 5;

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

	SilenceCountdown countdown_;
	int shown_ = -1;
	QTimer tick_, slow_;
	QPointer<QLineEdit> levelField_; // the level in the manager's settings, while they are open

	mutable std::mutex statusLock_;
	Status status_;
};

} // namespace sarxina::keeptalking
