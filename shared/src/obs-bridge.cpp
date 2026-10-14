#include "obs-bridge.hpp"

#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QTimer>

namespace sarxina {

namespace {

// How long a stream may sit in "asked for" or "connecting" with no word from OBS before the panel gives up on it.
// OBS reports nothing when it refuses to start (for example when a start is cancelled in one of its own dialogs).
constexpr double kRequestTimeoutMs = 10000.0;
constexpr double kStartingTimeoutMs = 30000.0;

// Calls into OBS are made from the event loop rather than from inside the panel's own step: OBS may answer with a
// dialog of its own, and the panel must not be half way through a step while that dialog is open.
template<typename Call> void later(Call call)
{
	QTimer::singleShot(0, qApp, call);
}

} // namespace

void ObsBridge::handleEvent(enum obs_frontend_event event, double now)
{
	now_ = now;
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		if (stream_ != Stream::Live) {
			stream_ = Stream::Starting;
			streamSince_ = now;
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		stream_ = Stream::Live;
		streamSince_ = now;
		if (stopWhenLive_) {
			stopWhenLive_ = false;
			later(obs_frontend_streaming_stop);
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		stream_ = Stream::Offline;
		stopWhenLive_ = false;
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		recording_ = true;
		recordingSince_ = now;
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		recording_ = false;
		break;
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
		virtualCam_ = true;
		break;
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
		virtualCam_ = false;
		break;
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
		studioMode_ = true;
		break;
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		studioMode_ = false;
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		refresh(now);
		break;
	default:
		break;
	}
}

void ObsBridge::follow(double now)
{
	now_ = now;
	if (now - refreshedAt_ >= 1000.0)
		refresh(now);
}

void ObsBridge::refresh(double now)
{
	now_ = now;
	refreshedAt_ = now;
	if (closed_)
		return;

	const bool live = obs_frontend_streaming_active();
	if (live && stream_ != Stream::Live) {
		stream_ = Stream::Live;
		streamSince_ = now;
	} else if (!live && stream_ == Stream::Live) {
		stream_ = Stream::Offline;
		stopWhenLive_ = false;
	} else if (!live && ((stream_ == Stream::Requested && now - streamSince_ > kRequestTimeoutMs) ||
			     (stream_ == Stream::Starting && now - streamSince_ > kStartingTimeoutMs))) {
		stream_ = Stream::Offline;
		stopWhenLive_ = false;
	}

	const bool recording = obs_frontend_recording_active();
	if (recording && !recording_)
		recordingSince_ = now;
	recording_ = recording;

	virtualCam_ = obs_frontend_virtualcam_active();
	studioMode_ = obs_frontend_preview_program_mode_active();
}

void ObsBridge::apply(PanelState &state) const
{
	const bool starting = stream_ == Stream::Requested || stream_ == Stream::Starting;
	state.set(Control::Stream, stream_ == Stream::Live || (starting && !stopWhenLive_), streamSince_);
	state.connecting = starting;
	state.set(Control::Record, recording_, recordingSince_);
	state.on[index(Control::VirtualCam)] = virtualCam_;
	state.on[index(Control::Studio)] = studioMode_;
}

void ObsBridge::press(Control control, bool on)
{
	if (closed_)
		return;
	switch (control) {
	case Control::Stream:
		setStreaming(on);
		break;
	case Control::Record:
		later(on ? obs_frontend_recording_start : obs_frontend_recording_stop);
		break;
	case Control::VirtualCam:
		later(on ? obs_frontend_start_virtualcam : obs_frontend_stop_virtualcam);
		break;
	case Control::Studio:
		later([on] { obs_frontend_set_preview_program_mode(on); });
		break;
	case Control::Settings:
		openSettings();
		break;
	case Control::Exit: // the gag never closes OBS
		break;
	}
}

void ObsBridge::setStreaming(bool on)
{
	const bool starting = stream_ == Stream::Requested || stream_ == Stream::Starting;
	if (on) {
		if (starting) {
			stopWhenLive_ = false;
		} else if (stream_ == Stream::Offline) {
			stream_ = Stream::Requested;
			streamSince_ = now_;
			later(obs_frontend_streaming_start);
		}
	} else if (starting) {
		// OBS cannot cancel a stream that is still connecting, so stop it the moment it is live.
		stopWhenLive_ = true;
	} else if (stream_ == Stream::Live) {
		later(obs_frontend_streaming_stop);
	}
}

void ObsBridge::openSettings()
{
	// The frontend API has no call for this, so use the same action as File > Settings.
	later([] {
		auto *window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (!window)
			return;
		if (QAction *action = window->findChild<QAction *>(QStringLiteral("action_Settings")))
			action->trigger();
	});
}

} // namespace sarxina
