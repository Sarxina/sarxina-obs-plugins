#include "source-menu.hpp"

#include "manager.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QTimer>

namespace sarxina {

namespace {

// The scene the streamer is editing: the preview's in Studio Mode, else the one on air. A new reference, or null.
obs_source_t *sceneBeingEdited()
{
	return obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene()
							    : obs_frontend_get_current_scene();
}

// The name, or the name with the first number after it that no source has yet.
QString unusedName(const QString &name)
{
	QString tried = name;
	for (int n = 2;; n++) {
		OBSSourceAutoRelease taken = obs_get_source_by_name(tried.toUtf8().constData());
		if (!taken)
			return tried;
		tried = QStringLiteral("%1 %2").arg(name).arg(n);
	}
}

// Scales the item to fit the canvas, keeping its shape, centred.
void fitToCanvas(obs_sceneitem_t *item)
{
	obs_video_info video{};
	if (!obs_get_video_info(&video))
		return;
	const vec2 bounds{static_cast<float>(video.base_width), static_cast<float>(video.base_height)};
	const vec2 corner{0.0f, 0.0f};
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_pos(item, &corner);
}

bool isSourceType(const char *id)
{
	const char *type = nullptr;
	for (size_t i = 0; obs_enum_input_types(i, &type); i++)
		if (strcmp(type, id) == 0)
			return true;
	return false;
}

} // namespace

SourceMenu::SourceMenu(Manager &manager, QObject *parent) : QObject(parent), manager_(manager) {}

bool SourceMenu::offer(const sarxina_source_offer &offer)
{
	if (!offer.plugin_id || !offer.source_id || !offer.name || !*offer.name)
		return false;
	if (!manager_.plugin(QString::fromUtf8(offer.plugin_id)) || !isSourceType(offer.source_id))
		return false;
	offers_.push_back({QString::fromUtf8(offer.plugin_id), QString::fromUtf8(offer.source_id),
			   QString::fromUtf8(offer.name)});
	return true;
}

std::vector<SourceOffer> SourceMenu::available() const
{
	std::vector<SourceOffer> on;
	for (const SourceOffer &offer : offers_) {
		const Plugin *plugin = manager_.plugin(offer.pluginId);
		if (plugin && plugin->enabled)
			on.push_back(offer);
	}
	return on;
}

bool SourceMenu::install(QWidget *mainWindow)
{
	window_ = mainWindow;
	sources_ = mainWindow ? mainWindow->findChild<QWidget *>(QStringLiteral("sources")) : nullptr;
	if (!sources_)
		return false;
	sources_->installEventFilter(this);
	if (auto *view = qobject_cast<QAbstractItemView *>(sources_.data()))
		view->viewport()->installEventFilter(this);
	qApp->installEventFilter(this);
	return true;
}

bool SourceMenu::eventFilter(QObject *watched, QEvent *event)
{
	const bool onSources = sources_ && (watched == sources_ || watched->parent() == sources_);
	if (onSources && event->type() == QEvent::ContextMenu) {
		armed_ = true;
		QTimer::singleShot(0, this, [this] { armed_ = false; }); // only the menu shown for this right-click
	} else if (armed_ && event->type() == QEvent::Polish) {
		auto *menu = qobject_cast<QMenu *>(watched);
		if (menu && menu->parentWidget() == window_) {
			armed_ = false;
			addSubmenu(menu);
		}
	}
	return false;
}

void SourceMenu::addSubmenu(QMenu *menu) const
{
	const std::vector<SourceOffer> offers = available();
	if (offers.empty())
		return;
	menu->addSeparator();
	QMenu *submenu = menu->addMenu(QString::fromUtf8(obs_module_text("SarxinaSource")));
	for (const SourceOffer &offer : offers)
		submenu->addAction(offer.name, submenu, [offer] { addToScene(offer); });
}

bool SourceMenu::addToScene(const SourceOffer &offer)
{
	OBSSourceAutoRelease sceneSource = sceneBeingEdited();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return false;
	OBSSourceAutoRelease source = obs_source_create(offer.sourceId.toUtf8().constData(),
							unusedName(offer.name).toUtf8().constData(), nullptr, nullptr);
	if (!source)
		return false;
	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (!item)
		return false;
	fitToCanvas(item);
	blog(LOG_INFO, "[%s] added %s to the scene", PLUGIN_TITLE, obs_source_get_name(source));
	return true;
}

} // namespace sarxina
