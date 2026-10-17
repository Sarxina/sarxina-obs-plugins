#include "manager-dock.hpp"

#include "properties-form.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

namespace sarxina {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QLabel *plain(const QString &words, QWidget *parent)
{
	auto *label = new QLabel(words, parent);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	return label;
}

QString name(const Plugin *plugin)
{
	return QString::fromUtf8(plugin->info->name);
}

} // namespace

ManagerDock::ManagerDock(Manager &manager, QWidget *parent) : QWidget(parent), manager_(manager)
{
	// Everything scrolls when the dock is shorter than it: nothing gets squeezed.
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *content = new QWidget(scroll);
	auto *layout = new QVBoxLayout(content);
	scroll->setWidget(content);
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->addWidget(scroll);

	// Connections
	auto *connections = new QGroupBox(text("Connections"), content);
	auto *grid = new QGridLayout(connections);
	grid->setColumnStretch(1, 1);

	// Twitch: no settings of its own, it follows the account OBS is connected to.
	twitchState_ = plain(QString(), connections);
	grid->addWidget(plain(text("Twitch"), connections), 0, 0);
	grid->addWidget(twitchState_, 0, 1, 1, 2);

	port_ = new QSpinBox(connections);
	port_->setRange(1, 65535);
	port_->setValue(manager.vtsPort());
	vtsButton_ = new QPushButton(connections);
	vtsState_ = plain(QString(), connections);
	QLabel *vts = plain(text("VTubeStudio"), connections);
	vts->setBuddy(port_);
	grid->addWidget(vts, 1, 0);
	grid->addWidget(port_, 1, 1);
	grid->addWidget(vtsButton_, 1, 2);
	grid->addWidget(vtsState_, 2, 1, 1, 2);
	layout->addWidget(connections);

	connect(&manager.twitch(), &TwitchChat::stateChanged, this, &ManagerDock::showTwitch);
	connect(vtsButton_, &QPushButton::clicked, this, &ManagerDock::pressVts);
	connect(&manager.vts(), &VTubeStudio::stateChanged, this, &ManagerDock::showVts);
	showTwitch();
	showVts();

	// Plugins: the dropdown to choose them, and the list of those chosen
	auto *plugins = new QGroupBox(text("Plugins"), content);
	plugins_ = new QVBoxLayout(plugins);
	choose_ = new QToolButton(plugins);
	choose_->setText(text("Choose"));
	choose_->setToolButtonStyle(Qt::ToolButtonTextOnly);
	choose_->setPopupMode(QToolButton::InstantPopup);
	choose_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	choices_ = new QMenu(choose_);
	choose_->setMenu(choices_);
	plugins_->addWidget(choose_);
	empty_ = plain(QString(), plugins);
	plugins_->addWidget(empty_);
	layout->addWidget(plugins);
	layout->addStretch(1);
	for (const auto &plugin : manager.plugins())
		addRow(plugin.get());
	showList();
	connect(&manager, &Manager::pluginRegistered, this, &ManagerDock::addRow);
	connect(&manager, &Manager::pluginChanged, this, &ManagerDock::showRow);
}

// ---- connections ----------------------------------------------------------------------------------------------------

void ManagerDock::showTwitch()
{
	const QString channel = manager_.twitch().channel();
	switch (manager_.twitch().state()) {
	case TwitchChat::State::Off: // the OBS profile has no Twitch account
		twitchState_->setText(text("Twitch.NoAccount"));
		break;
	case TwitchChat::State::Connecting:
		twitchState_->setText(text("Twitch.Connecting").arg(channel));
		break;
	case TwitchChat::State::Reading:
		twitchState_->setText(text("Twitch.Reading").arg(channel));
		break;
	case TwitchChat::State::Retrying:
		twitchState_->setText(text("Twitch.Retrying"));
		break;
	}
}

void ManagerDock::pressVts()
{
	if (manager_.vts().state() == VTubeStudio::State::Off ||
	    manager_.vts().state() == VTubeStudio::State::NotAllowed)
		manager_.connectVts(port_->value());
	else
		manager_.disconnectVts();
}

void ManagerDock::showVts()
{
	const VTubeStudio::State state = manager_.vts().state();
	const bool off = state == VTubeStudio::State::Off || state == VTubeStudio::State::NotAllowed;
	vtsButton_->setText(text(off ? "Connect" : "Disconnect"));
	port_->setEnabled(off);
	switch (state) {
	case VTubeStudio::State::Off: vtsState_->setText(text("Off")); break;
	case VTubeStudio::State::Connecting: vtsState_->setText(text("VTS.Connecting")); break;
	case VTubeStudio::State::Allowing: vtsState_->setText(text("VTS.Allowing")); break;
	case VTubeStudio::State::Ready: vtsState_->setText(text("VTS.Ready")); break;
	case VTubeStudio::State::NotAllowed: vtsState_->setText(text("VTS.NotAllowed")); break;
	case VTubeStudio::State::Retrying:
		vtsState_->setText(text("VTS.Retrying").arg(manager_.vts().port()));
		break;
	}
}

// ---- plugins --------------------------------------------------------------------------------------------------------

// A plugin's check box in the dropdown, and its line in the list, one line however many there are: its name and the
// gear. What it does folds out under the name. The line shows only while the plugin is added.
void ManagerDock::addRow(Plugin *plugin)
{
	// The check box, with room around it like the other entries of a menu.
	auto *holder = new QWidget(choices_);
	auto *margins = new QVBoxLayout(holder);
	margins->setContentsMargins(8, 3, 16, 3);
	auto *pick = new QCheckBox(name(plugin), holder);
	pick->setToolTip(QString::fromUtf8(plugin->info->description ? plugin->info->description : ""));
	margins->addWidget(pick);
	auto *action = new QWidgetAction(choices_);
	action->setDefaultWidget(holder);
	choices_->addAction(action);
	connect(pick, &QCheckBox::toggled, this, [this, plugin](bool added) { manager_.setEnabled(*plugin, added); });

	QWidget *list = plugins_->parentWidget();
	auto *line = new QWidget(list);
	auto *grid = new QGridLayout(line);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setColumnStretch(0, 1);

	// The name opens and closes the description, with OBS's own arrow (the one that folds groups in Sources).
	// Checked means folded, as in OBS.
	auto *title = new QCheckBox(name(plugin), line);
	title->setProperty("class", "indicator-expand");
	title->setChecked(true);
	QFont bold = title->font();
	bold.setBold(true);
	title->setFont(bold);
	QLabel *about = plain(QString::fromUtf8(plugin->info->description ? plugin->info->description : ""), line);
	about->setContentsMargins(style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, title) +
					  style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, title),
				  0, 0, 0);
	about->hide();
	connect(title, &QCheckBox::toggled, about, [about](bool folded) { about->setVisible(!folded); });

	auto *gear = new QToolButton(line);
	gear->setProperty("class", "icon-gear"); // OBS's own gear, from its theme
	gear->setText(QStringLiteral("⚙"));  // when there is no theme
	gear->setToolTip(text("Settings").arg(name(plugin)));
	gear->setAccessibleName(gear->toolTip());
	connect(gear, &QToolButton::clicked, this, [this, plugin] { openSettings(plugin); });

	grid->addWidget(title, 0, 0);
	grid->addWidget(gear, 0, 1);
	grid->addWidget(about, 1, 0, 1, 2);
	plugins_->addWidget(line);

	rows_[plugin] = {line, gear, pick};
	showRow(plugin);
}

void ManagerDock::showRow(Plugin *plugin)
{
	const auto found = rows_.find(plugin);
	if (found == rows_.end())
		return;
	Row &row = found->second;
	{
		const QSignalBlocker quiet(row.pick);
		row.pick->setChecked(plugin->enabled);
	}
	row.line->setVisible(plugin->enabled);
	// Only a plugin that describes settings has a gear.
	row.gear->setVisible(plugin->info->get_properties != nullptr);
	if (row.settings && !plugin->enabled)
		row.settings->close();
	if (row.form)
		row.form->reload();
	showList();
}

// What the list says when it is empty, and the dropdown only when there is something to choose.
void ManagerDock::showList()
{
	const bool installed = !rows_.empty();
	const bool added = std::any_of(rows_.begin(), rows_.end(), [](const auto &row) { return row.first->enabled; });
	choose_->setEnabled(installed);
	empty_->setText(text(installed ? "NoneAdded" : "NoPlugins"));
	empty_->setVisible(!added);
}

void ManagerDock::openSettings(Plugin *plugin)
{
	Row &row = rows_.at(plugin);
	if (!row.settings) {
		row.settings = new QDialog(this);
		row.settings->setWindowTitle(text("Settings").arg(name(plugin)));
		auto *layout = new QVBoxLayout(row.settings);
		row.form = new PropertiesForm(*plugin, row.settings);
		layout->addWidget(row.form);
		layout->addStretch(1);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, row.settings);
		connect(buttons, &QDialogButtonBox::rejected, row.settings, &QDialog::close);
		layout->addWidget(buttons);
		row.settings->resize(row.settings->sizeHint().expandedTo(QSize(420, 0)));
	} else {
		row.form->reload();
	}
	row.settings->show();
	row.settings->raise();
	row.settings->activateWindow();
}

} // namespace sarxina
