// "Sarxina Source" in OBS's Sources list: right-clicking the list shows a submenu of the sources Sarxina plugins offer
// (sarxina_obs_plugin_manager_offer_source), and picking one adds it to the scene being edited, filling the canvas.
//
// OBS builds that menu itself each time and has no way for plugins to add to it, so the submenu goes in as OBS shows
// the menu: a right-click on the Sources list is noted, and the next menu OBS's window polishes for it gets the
// submenu. Only sources of plugins that are on are listed, and the submenu is left out when there are none.
#pragma once

#include "sarxina-obs-plugin-manager.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <vector>

class QMenu;
class QWidget;

namespace sarxina {

class Manager;

struct SourceOffer {
	QString pluginId;
	QString sourceId;
	QString name;
};

class SourceMenu : public QObject {
public:
	explicit SourceMenu(Manager &manager, QObject *parent = nullptr);

	// A plugin offers a source. False when it is not one: no plugin of that id, or no source type of that id.
	bool offer(const sarxina_source_offer &offer);
	// The sources offered by plugins that are on, in the order they were offered.
	std::vector<SourceOffer> available() const;

	// Hooks the Sources list of OBS's main window. False when the list is not there.
	bool install(QWidget *mainWindow);
	// Adds a source of the offer to the scene being edited, filling the canvas. False when it cannot be made.
	static bool addToScene(const SourceOffer &offer);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void addSubmenu(QMenu *menu) const;

	Manager &manager_;
	std::vector<SourceOffer> offers_;
	QPointer<QWidget> window_;
	QPointer<QWidget> sources_;
	bool armed_ = false; // the Sources list was right-clicked, and its menu is coming
};

} // namespace sarxina
