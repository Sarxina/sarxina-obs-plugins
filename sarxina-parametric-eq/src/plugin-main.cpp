// Sarxina Parametric EQ as an OBS plugin: an audio filter (Filters > Audio Filters > + on any audio source). Its
// properties have one button, which opens the curve editor; the editor sends every change straight to the filter,
// and OBS saves it with the scene collection like any other filter setting.
#include "curve-editor.hpp"
#include "eq-design.hpp"
#include "eq-settings.hpp"
#include "self-test.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <mutex>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return PLUGIN_TITLE;
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

namespace {

using namespace sarxina::eq;

struct Filter {
	obs_source_t *source = nullptr;
	double sampleRate = 48000;
	size_t channels = 2;

	// A new design waits here until the audio thread picks it up between two blocks. The lock also covers
	// `editor`, which only the UI thread changes but OBS can update or destroy the filter from other threads.
	std::mutex lock;
	Design pending;
	bool hasPending = false;
	QPointer<CurveEditor> editor;

	Processor processor; // audio thread only

	QPointer<CurveEditor> currentEditor()
	{
		std::lock_guard<std::mutex> hold(lock);
		return editor;
	}
};

const char *filterName(void *)
{
	return obs_module_text("FilterName");
}

void filterUpdate(void *data, obs_data_t *settingsData)
{
	auto *f = static_cast<Filter *>(data);
	const Settings settings = read(settingsData);
	QPointer<CurveEditor> editor;
	{
		std::lock_guard<std::mutex> hold(f->lock);
		f->pending = Design::of(settings, f->sampleRate);
		f->hasPending = true;
		editor = f->editor;
	}
	// Keep an open editor in step with changes made elsewhere. Its own changes come back here too, unchanged.
	QMetaObject::invokeMethod(
		qApp,
		[editor, settings] {
			if (editor)
				editor->setSettings(settings);
		},
		Qt::QueuedConnection);
}

void *filterCreate(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new Filter();
	f->source = source;
	f->sampleRate = audio_output_get_sample_rate(obs_get_audio());
	f->channels = audio_output_get_channels(obs_get_audio());
	f->processor.setDesign(Design::of(read(settings), f->sampleRate));
	return f;
}

void filterDestroy(void *data)
{
	auto *f = static_cast<Filter *>(data);
	QPointer<CurveEditor> editor = f->currentEditor();
	QMetaObject::invokeMethod(
		qApp,
		[editor] {
			if (editor)
				editor->deleteLater();
		},
		Qt::QueuedConnection);
	delete f;
}

obs_audio_data *filterAudio(void *data, obs_audio_data *audio)
{
	auto *f = static_cast<Filter *>(data);
	{
		// Never wait on the UI: if it is busy handing over a design, take it with the next block.
		std::unique_lock<std::mutex> hold(f->lock, std::try_to_lock);
		if (hold.owns_lock() && f->hasPending) {
			f->processor.setDesign(f->pending);
			f->hasPending = false;
		}
	}
	const size_t channels = std::min<size_t>(f->channels, Processor::maxChannels);
	for (size_t c = 0; c < channels; c++) {
		if (audio->data[c])
			f->processor.process(reinterpret_cast<float *>(audio->data[c]), audio->frames, static_cast<int>(c));
	}
	return audio;
}

void filterDefaults(obs_data_t *settings)
{
	setDefaults(settings);
}

// Opens the curve editor for this filter, or brings it to the front.
bool openEditor(obs_properties_t *, obs_property_t *, void *data)
{
	auto *f = static_cast<Filter *>(data);
	QPointer<CurveEditor> editor = f->currentEditor();
	if (!editor) {
		obs_data_t *settingsData = obs_source_get_settings(f->source);
		const Settings settings = read(settingsData);
		obs_data_release(settingsData);

		obs_weak_source_t *weak = obs_source_get_weak_source(f->source);
		auto changed = [weak](const Settings &edited) {
			obs_source_t *source = obs_weak_source_get_source(weak);
			if (!source)
				return;
			obs_data_t *settingsData = obs_source_get_settings(source);
			write(settingsData, edited);
			obs_source_update(source, settingsData);
			obs_data_release(settingsData);
			obs_source_release(source);
		};
		auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
		editor = new CurveEditor(settings, f->sampleRate, changed, parent);
		editor->setAttribute(Qt::WA_DeleteOnClose);
		QObject::connect(editor, &QObject::destroyed, [weak] { obs_weak_source_release(weak); });
		{
			std::lock_guard<std::mutex> hold(f->lock);
			f->editor = editor;
		}

		obs_source_t *parentSource = obs_filter_get_parent(f->source);
		const char *sourceName = parentSource ? obs_source_get_name(parentSource) : nullptr;
		if (sourceName && *sourceName)
			editor->setWindowTitle(editor->windowTitle() + " - " + QString::fromUtf8(sourceName));
	}
	editor->show();
	editor->raise();
	editor->activateWindow();
	return false;
}

obs_properties_t *filterProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_button2(props, "open_editor", obs_module_text("OpenEditor"), openEditor, data);
	return props;
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING &&
	    qEnvironmentVariableIntValue("SARXINA_PARAMETRIC_EQ_SELFTEST") != 0)
		runSelfTest(qgetenv("SARXINA_PARAMETRIC_EQ_SELFTEST_PICTURES").constData());
}

} // namespace

bool obs_module_load(void)
{
	obs_source_info info = {};
	info.id = "sarxina_parametric_eq";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = filterName;
	info.create = filterCreate;
	info.destroy = filterDestroy;
	info.update = filterUpdate;
	info.filter_audio = filterAudio;
	info.get_defaults = filterDefaults;
	info.get_properties = filterProperties;
	obs_register_source(&info);

	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	blog(LOG_INFO, "[%s] %s loaded", PLUGIN_TITLE, PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
