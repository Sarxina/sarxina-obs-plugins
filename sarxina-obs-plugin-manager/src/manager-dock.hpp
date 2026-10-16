// The Sarxina OBS Plugin Manager's dock: the Twitch chat and VTube Studio connections with a line of state each, and
// the list of Sarxina plugins. Each one can be added to OBS or removed; an added one has a gear that opens its
// settings, drawn from what the plugin says about them.
#pragma once

#include "manager.hpp"

#include <QWidget>

#include <map>

class QDialog;
class QLabel;
class QLineEdit;
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
		QToolButton *gear;
		QPushButton *add;
		QDialog *settings = nullptr; // made the first time the gear is pressed
		PropertiesForm *form = nullptr;
	};

	void pressTwitch();
	void pressVts();
	void showTwitch();
	void showVts();
	void addRow(Plugin *plugin);
	void showRow(Plugin *plugin);
	void openSettings(Plugin *plugin);

	Manager &manager_;
	QLineEdit *channel_;
	QPushButton *twitchButton_;
	QLabel *twitchState_;
	bool badChannel_ = false;
	QSpinBox *port_;
	QPushButton *vtsButton_;
	QLabel *vtsState_;
	QVBoxLayout *plugins_;
	QLabel *noPlugins_;
	std::map<Plugin *, Row> rows_;
};

} // namespace sarxina
