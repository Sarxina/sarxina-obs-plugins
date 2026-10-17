// The Sarxina OBS Plugin Manager's dock: a line of state for the Twitch chat (of the account OBS is connected to) and
// for VTube Studio, and the streamer's list of plugins. A dropdown holds every Sarxina plugin that is installed, each
// with a check box: checking one adds it to OBS and to the list, unchecking takes it away again. In the list each
// plugin is one line, its name, which folds out what it does, and a gear that opens its settings, drawn from what the
// plugin says about them.
#pragma once

#include "manager.hpp"

#include <QWidget>

#include <map>

class QCheckBox;
class QDialog;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

namespace sarxina {

class PropertiesForm;

class ManagerDock : public QWidget {
	Q_OBJECT

public:
	explicit ManagerDock(Manager &manager, QWidget *parent = nullptr);

	QSize sizeHint() const override { return {380, 420}; }

private:
	struct Row {
		QWidget *line;   // in the list, shown while the plugin is added
		QToolButton *gear;
		QCheckBox *pick; // in the dropdown
		QDialog *settings = nullptr; // made the first time the gear is pressed
		PropertiesForm *form = nullptr;
	};

	void pressVts();
	void showTwitch();
	void showVts();
	void addRow(Plugin *plugin);
	void showRow(Plugin *plugin);
	void showList();
	void openSettings(Plugin *plugin);

	Manager &manager_;
	QLabel *twitchState_;
	QSpinBox *port_;
	QPushButton *vtsButton_;
	QLabel *vtsState_;
	QToolButton *choose_; // the dropdown
	QMenu *choices_;
	QVBoxLayout *plugins_;
	QLabel *empty_; // when the list is empty: why
	std::map<Plugin *, Row> rows_;
};

} // namespace sarxina
