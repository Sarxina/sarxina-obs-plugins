// The Sarxina OBS Plugin Manager's dock: a line of state for the Twitch chat (of the account OBS is connected to) and
// for VTube Studio, and the streamer's plugins. A dropdown holds every Sarxina plugin there is, each with an Install
// button that reads Uninstall once it is installed, and Update when there is a newer version. OBS loads plugins only
// when it starts, so after a change the plugin's line in the dropdown says to restart OBS, and so does the dock.
//
// Below the dropdown is the list of plugins OBS runs, one line each: its name, which folds out what it does, and a
// gear that opens its settings, drawn from what the plugin says about them.
#pragma once

#include "manager.hpp"

#include <QWidget>

#include <map>
#include <vector>

class QCheckBox;
class QDialog;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;
class QWidgetAction;

namespace sarxina {

class PropertiesForm;

class ManagerDock : public QWidget {
	Q_OBJECT

public:
	explicit ManagerDock(Manager &manager, QWidget *parent = nullptr);

	QSize sizeHint() const override { return {380, 420}; }

private:
	struct Row {
		// In the dropdown
		QWidgetAction *choice;
		QLabel *name;
		QLabel *note;
		QPushButton *update;
		QPushButton *install; // Install or Uninstall
		// In the list, shown while OBS runs the plugin
		QWidget *line;
		QCheckBox *title;
		QLabel *about;
		QToolButton *gear;
		QDialog *settings = nullptr; // made the first time the gear is pressed
		PropertiesForm *form = nullptr;
	};

	void pressVts();
	void showTwitch();
	void showVts();
	void addRows();
	Row &addRow(const QString &id);
	void showRow(const QString &id);
	void showList();
	void openSettings(const QString &id);

	Manager &manager_;
	QLabel *twitchState_;
	QSpinBox *port_;
	QPushButton *vtsButton_;
	QLabel *vtsState_;
	QToolButton *choose_; // the dropdown
	QMenu *choices_;
	QWidgetAction *listState_; // at the top of the dropdown, while the list of plugins is not there
	QVBoxLayout *plugins_;
	QLabel *empty_;   // when OBS runs none
	QLabel *restart_; // when a change waits for OBS to restart
	std::vector<QString> order_;
	std::map<QString, Row> rows_;
};

} // namespace sarxina
