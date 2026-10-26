#include "filters.hpp"

#include "curve-editor.hpp"
#include "eq-settings.hpp"

#include <obs-frontend-api.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>

namespace sarxina::voice {

namespace {

const Kind allKinds[] = {
	{"voicegoodifier_chain", "ChainName", {AllStages}, false},
	{"voicegoodifier_gate", "GateName", {GateStage}, false},
	{"voicegoodifier_eq", "EqName", {EqStage}, false},
	{"voicegoodifier_limiter", "LimiterName", {LimiterStage}, false},
	{"sarxina_noise_gate", "GateName", {GateStage}, true},
	{"sarxina_parametric_eq", "EqName", {EqStage}, true},
	{"sarxina_hard_limiter", "LimiterName", {LimiterStage}, true},
};

struct Filter {
	const Kind *kind = nullptr;
	obs_source_t *source = nullptr;
	double sampleRate = 48000;
	size_t channels = 2;

	// New settings wait here until the audio thread picks them up between two blocks. The lock also covers
	// `editor`, which only the UI thread changes but OBS can update or destroy the filter from other threads.
	std::mutex lock;
	ChainSettings pending;
	bool hasPending = false;
	QPointer<eq::CurveEditor> editor;

	std::unique_ptr<Chain> chain; // audio thread only

	QPointer<eq::CurveEditor> currentEditor()
	{
		std::lock_guard<std::mutex> hold(lock);
		return editor;
	}
	const char *eqPrefix() const { return kind->layout.prefix(EqStage); }
};

const Kind *kindOf(const char *id)
{
	for (const Kind &kind : allKinds) {
		if (std::strcmp(kind.id, id) == 0)
			return &kind;
	}
	return nullptr;
}

const char *filterName(void *typeData)
{
	return obs_module_text(static_cast<const Kind *>(typeData)->nameKey);
}

// Keeps an open curve editor in step with EQ settings changed elsewhere (OBS's undo, a websocket request). The
// editor's own changes come back here too, unchanged.
void showInEditor(QPointer<eq::CurveEditor> editor, const eq::Settings &settings)
{
	QMetaObject::invokeMethod(
		qApp,
		[editor, settings] {
			if (editor)
				editor->setSettings(settings);
		},
		Qt::QueuedConnection);
}

void filterUpdate(void *data, obs_data_t *settingsData)
{
	auto *f = static_cast<Filter *>(data);
	const ChainSettings settings = read(settingsData, f->kind->layout, f->sampleRate);
	QPointer<eq::CurveEditor> editor;
	{
		std::lock_guard<std::mutex> hold(f->lock);
		f->pending = settings;
		f->hasPending = true;
		editor = f->editor;
	}
	if (editor)
		showInEditor(editor, eq::read(settingsData, f->eqPrefix()));
}

void *filterCreate(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new Filter();
	f->kind = kindOf(obs_source_get_id(source));
	f->source = source;
	f->sampleRate = audio_output_get_sample_rate(obs_get_audio());
	f->channels = audio_output_get_channels(obs_get_audio());
	f->chain = std::make_unique<Chain>(f->kind->layout, f->sampleRate);
	f->chain->setSettings(read(settings, f->kind->layout, f->sampleRate));
	return f;
}

void filterDestroy(void *data)
{
	auto *f = static_cast<Filter *>(data);
	QPointer<eq::CurveEditor> editor = f->currentEditor();
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
		// Never wait on the UI: if it is busy handing over settings, take them with the next block.
		std::unique_lock<std::mutex> hold(f->lock, std::try_to_lock);
		if (hold.owns_lock() && f->hasPending) {
			f->chain->setSettings(f->pending);
			f->hasPending = false;
		}
	}
	float *planes[MAX_AV_PLANES] = {};
	int count = 0;
	for (size_t c = 0; c < std::min<size_t>(f->channels, MAX_AV_PLANES) && audio->data[c]; c++)
		planes[count++] = reinterpret_cast<float *>(audio->data[c]);
	f->chain->process(planes, count, audio->frames);
	return audio;
}

void filterDefaults(void *typeData, obs_data_t *settings)
{
	setDefaults(settings, static_cast<const Kind *>(typeData)->layout);
}

// A curve editor for the filter; every edit in it goes to the filter's settings.
eq::CurveEditor *makeEditor(Filter *f)
{
	obs_data_t *settingsData = obs_source_get_settings(f->source);
	const eq::Settings settings = eq::read(settingsData, f->eqPrefix());
	obs_data_release(settingsData);

	obs_weak_source_t *weak = obs_source_get_weak_source(f->source);
	const std::string prefix = f->eqPrefix();
	auto changed = [weak, prefix](const eq::Settings &edited) {
		obs_source_t *source = obs_weak_source_get_source(weak);
		if (!source)
			return;
		obs_data_t *settingsData = obs_source_get_settings(source);
		eq::write(settingsData, edited, prefix.c_str());
		obs_source_update(source, settingsData);
		obs_data_release(settingsData);
		obs_source_release(source);
	};
	auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	auto *editor = new eq::CurveEditor(settings, f->sampleRate, changed, parent);
	editor->setAttribute(Qt::WA_DeleteOnClose);
	QObject::connect(editor, &QObject::destroyed, [weak] { obs_weak_source_release(weak); });

	obs_source_t *parentSource = obs_filter_get_parent(f->source);
	const char *sourceName = parentSource ? obs_source_get_name(parentSource) : nullptr;
	if (sourceName && *sourceName)
		editor->setWindowTitle(editor->windowTitle() + " - " + QString::fromUtf8(sourceName));
	return editor;
}

// Opens the curve editor for this filter, or brings it to the front.
bool openEditor(obs_properties_t *, obs_property_t *, void *data)
{
	auto *f = static_cast<Filter *>(data);
	QPointer<eq::CurveEditor> editor = f->currentEditor();
	if (!editor) {
		editor = makeEditor(f);
		std::lock_guard<std::mutex> hold(f->lock);
		f->editor = editor;
	}
	editor->show();
	editor->raise();
	editor->activateWindow();
	return false;
}

obs_properties_t *filterProperties(void *data, void *typeData)
{
	const Layout &layout = static_cast<const Kind *>(typeData)->layout;
	obs_properties_t *props = obs_properties_create();
	if (layout.has(EqStage))
		obs_properties_add_button2(props, "open_editor", obs_module_text("OpenEditor"), openEditor, data);
	addStageProperties(props, layout);
	return props;
}

} // namespace

const Kind *kinds(size_t &count)
{
	count = std::size(allKinds);
	return allKinds;
}

void registerFilters()
{
	for (const Kind &kind : allKinds) {
		obs_source_info info = {};
		info.id = kind.id;
		info.type = OBS_SOURCE_TYPE_FILTER;
		info.output_flags = OBS_SOURCE_AUDIO | (kind.replaced ? OBS_SOURCE_DEPRECATED : 0);
		info.type_data = const_cast<Kind *>(&kind);
		info.get_name = filterName;
		info.create = filterCreate;
		info.destroy = filterDestroy;
		info.update = filterUpdate;
		info.filter_audio = filterAudio;
		info.get_defaults2 = filterDefaults;
		info.get_properties2 = filterProperties;
		obs_register_source(&info);
	}
}

void runFilter(obs_source_t *filter, obs_audio_data *audio)
{
	filterAudio(obs_obj_get_data(filter), audio);
}

} // namespace sarxina::voice
