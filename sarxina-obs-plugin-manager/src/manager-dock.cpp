#include "manager-dock.hpp"

#include "properties-form.hpp"

#include <obs-module.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

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

	channel_ = new QLineEdit(manager.channel(), connections);
	channel_->setPlaceholderText(text("Twitch.Channel"));
	channel_->setMaxLength(26);
	twitchButton_ = new QPushButton(connections);
	twitchState_ = plain(QString(), connections);
	QLabel *twitch = plain(text("Twitch"), connections);
	twitch->setBuddy(channel_);
	grid->addWidget(twitch, 0, 0);
	grid->addWidget(channel_, 0, 1);
	grid->addWidget(twitchButton_, 0, 2);
	grid->addWidget(twitchState_, 1, 1, 1, 2);

	port_ = new QSpinBox(connections);
	port_->setRange(1, 65535);
	port_->setValue(manager.vtsPort());
	vtsButton_ = new QPushButton(connections);
	vtsState_ = plain(QString(), connections);
	QLabel *vts = plain(text("VTubeStudio"), connections);
	vts->setBuddy(port_);
	grid->addWidget(vts, 2, 0);
	grid->addWidget(port_, 2, 1);
	grid->addWidget(vtsButton_, 2, 2);
	grid->addWidget(vtsState_, 3, 1, 1, 2);
	layout->addWidget(connections);

	connect(twitchButton_, &QPushButton::clicked, this, &ManagerDock::pressTwitch);
	connect(channel_, &QLineEdit::returnPressed, this, [this] {
		if (manager_.twitch().state() == TwitchChat::State::Off)
			pressTwitch();
	});
	connect(channel_, &QLineEdit::textEdited, this, [this] {
		badChannel_ = false;
		showTwitch();
	});
	connect(&manager.twitch(), &TwitchChat::stateChanged, this, &ManagerDock::showTwitch);
	connect(vtsButton_, &QPushButton::clicked, this, &ManagerDock::pressVts);
	connect(&manager.vts(), &VTubeStudio::stateChanged, this, &ManagerDock::showVts);
	showTwitch();
	showVts();

	// Plugins
	auto *plugins = new QGroupBox(text("Plugins"), content);
	plugins_ = new QVBoxLayout(plugins);
	noPlugins_ = plain(text("NoPlugins"), plugins);
	plugins_->addWidget(noPlugins_);
	layout->addWidget(plugins);
	layout->addStretch(1);
	for (const auto &plugin : manager.plugins())
		addRow(plugin.get());
	connect(&manager, &Manager::pluginRegistered, this, &ManagerDock::addRow);
	connect(&manager, &Manager::pluginChanged, this, &ManagerDock::showRow);
}

// ---- connections ----------------------------------------------------------------------------------------------------

void ManagerDock::pressTwitch()
{
	if (manager_.twitch().state() != TwitchChat::State::Off) {
		manager_.leaveTwitch();
		return;
	}
	QString channel = channel_->text().trimmed();
	if (channel.startsWith('#'))
		channel.remove(0, 1);
	badChannel_ = !TwitchChat::validChannel(channel);
	if (badChannel_)
		showTwitch();
	else
		manager_.joinTwitch(channel);
}

void ManagerDock::showTwitch()
{
	const TwitchChat::State state = manager_.twitch().state();
	const QString channel = manager_.twitch().channel();
	twitchButton_->setText(text(state == TwitchChat::State::Off ? "Connect" : "Disconnect"));
	channel_->setReadOnly(state != TwitchChat::State::Off);
	if (state != TwitchChat::State::Off)
		channel_->setText(channel);
	switch (state) {
	case TwitchChat::State::Off:
		twitchState_->setText(text(badChannel_ ? "Twitch.BadChannel" : "Off"));
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

// A plugin's row: its name with the gear and the Add or Remove button, and what it does underneath.
void ManagerDock::addRow(Plugin *plugin)
{
	noPlugins_->hide();
	QWidget *list = plugins_->parentWidget();
	auto *row = new QWidget(list);
	auto *grid = new QGridLayout(row);
	grid->setContentsMargins(0, 0, 0, 6);
	grid->setColumnStretch(0, 1);

	QLabel *title = plain(name(plugin), row);
	QFont bold = title->font();
	bold.setBold(true);
	title->setFont(bold);

	auto *gear = new QToolButton(row);
	gear->setProperty("class", "icon-gear"); // OBS's own gear, from its theme
	gear->setText(QStringLiteral("⚙"));  // when there is no theme
	gear->setToolTip(text("Settings").arg(name(plugin)));
	gear->setAccessibleName(gear->toolTip());
	connect(gear, &QToolButton::clicked, this, [this, plugin] { openSettings(plugin); });

	auto *add = new QPushButton(row);
	connect(add, &QPushButton::clicked, this, [this, plugin] { manager_.setEnabled(*plugin, !plugin->enabled); });

	grid->addWidget(title, 0, 0);
	grid->addWidget(gear, 0, 1);
	grid->addWidget(add, 0, 2);
	grid->addWidget(plain(QString::fromUtf8(plugin->info->description ? plugin->info->description : ""), row), 1, 0,
			1, 3);
	plugins_->addWidget(row);

	rows_[plugin] = {gear, add};
	showRow(plugin);
}

void ManagerDock::showRow(Plugin *plugin)
{
	const auto found = rows_.find(plugin);
	if (found == rows_.end())
		return;
	Row &row = found->second;
	row.add->setText(text(plugin->enabled ? "Remove" : "Add"));
	row.add->setAccessibleName(text(plugin->enabled ? "Remove.Named" : "Add.Named").arg(name(plugin)));
	// Only an added plugin has settings to change, and only if it describes some.
	row.gear->setVisible(plugin->enabled && plugin->info->get_properties);
	if (row.settings && !plugin->enabled)
		row.settings->close();
	if (row.form)
		row.form->reload();
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
