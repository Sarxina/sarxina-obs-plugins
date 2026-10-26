// The Sarxina OBS Plugin Manager's dock: a line for the one Twitch connection (with Connect, or the code to enter at
// twitch.tv/activate, then the account and its chat) and one for VTube Studio, and the streamer's plugins. A dropdown
// holds every Sarxina plugin there is, each with an Install button that reads Uninstall once it is installed. OBS loads
// plugins only when it starts, so after a change the plugin's line in the dropdown says to restart OBS, and so does the
// dock.
//
// The dropdown is a list that scrolls once it holds more than a handful, one slim line each: the plugin's name, its
// button and, when there is one, a note under it.
//
// Below the dropdown is the list of plugins OBS runs, one line each: its name, which folds out what it does, and a
// gear that opens its settings, drawn from what the plugin says about them. An installed plugin with a newer version
// has a small up arrow beside its name, in the list and in the dropdown, that installs the newer version.
//
// When there is a newer version of the manager itself, a line at the top of the dock has the same up arrow, which
// updates the manager; the new one runs once OBS restarts.
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
class QScrollArea;
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
		QWidget *choice;
		QLabel *name;
		QLabel *note;
		QToolButton *update;  // the up arrow, while there is a newer version
		QToolButton *install; // Install or Uninstall
		// In the list, shown while OBS runs the plugin
		QWidget *line;
		QCheckBox *title;
		QToolButton *lineUpdate; // the up arrow, as in the dropdown
		QLabel *about;
		QToolButton *gear;
		QDialog *settings = nullptr; // made the first time the gear is pressed
		PropertiesForm *form = nullptr;
	};

	void pressVts();
	void pressTwitch();
	void showTwitch();
	void showVts();
	QWidget *addManagerLine(QWidget *parent);
	void showManager();
	QWidget *addChoices();
	void fitChoices();
	void addRows();
	Row &addRow(const QString &id);
	QWidget *addChoice(const QString &id, Row &row);
	QWidget *addLine(const QString &id, Row &row);
	QToolButton *upArrow(QWidget *parent);
	void showRow(const QString &id);
	void showList();
	void openSettings(const QString &id);

	Manager &manager_;
	QWidget *managerLine_; // shown while the manager has something to say about itself
	QLabel *managerState_;
	QToolButton *managerArrow_;
	QLabel *twitchState_;
	QPushButton *twitchButton_; // Connect, Open, Disconnect or Reconnect
	QLabel *chatState_;         // the chat of the account connected, under it
	QSpinBox *port_;
	QPushButton *vtsButton_;
	QLabel *vtsState_;
	QToolButton *choose_; // the dropdown
	QMenu *choices_;
	QWidgetAction *listState_; // at the top of the dropdown, while the list of plugins is not there
	QScrollArea *choicesView_; // the dropdown's lines, scrolling
	QVBoxLayout *choiceRows_;
	QVBoxLayout *plugins_;
	QLabel *empty_;   // when OBS runs none
	QLabel *restart_; // when a change waits for OBS to restart
	std::vector<QString> order_;
	std::map<QString, Row> rows_;
};

} // namespace sarxina
