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

	// Plugins: the dropdown with every one there is, then the list of those OBS runs
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

	// While the list is not there: why, at the top of the dropdown. It is fetched again when the dropdown opens.
	auto *holder = new QWidget(choices_);
	auto *margins = new QVBoxLayout(holder);
	margins->setContentsMargins(8, 4, 16, 4);
	margins->addWidget(plain(QString(), holder));
	listState_ = new QWidgetAction(this);
	listState_->setDefaultWidget(holder);
	choices_->addAction(listState_);
	connect(choices_, &QMenu::aboutToShow, this, [this] {
		if (manager_.store().list() == Store::List::Failed)
			manager_.store().refresh();
	});

	empty_ = plain(text("NoneInstalled"), plugins);
	plugins_->addWidget(empty_);
	restart_ = plain(text("Restart"), plugins);
	plugins_->addWidget(restart_);
	layout->addWidget(plugins);
	layout->addStretch(1);

	addRows();
	connect(&manager, &Manager::pluginRegistered, this, &ManagerDock::addRows);
	connect(&manager, &Manager::pluginChanged, this,
		[this](Plugin *plugin) { showRow(QString::fromUtf8(plugin->info->id)); });
	connect(&manager.store(), &Store::listChanged, this, &ManagerDock::addRows);
	connect(&manager.store(), &Store::changed, this, &ManagerDock::showRow);
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

// A row for every plugin: those on the list, in its order, then any other that registered.
void ManagerDock::addRows()
{
	std::vector<QString> ids;
	for (const Available &plugin : manager_.store().available())
		ids.push_back(plugin.id);
	for (const auto &plugin : manager_.plugins()) {
		const QString id = QString::fromUtf8(plugin->info->id);
		if (std::find(ids.begin(), ids.end(), id) == ids.end())
			ids.push_back(id);
	}

	if (ids != order_) {
		for (auto &[id, row] : rows_) {
			choices_->removeAction(row.choice);
			plugins_->removeWidget(row.line);
			row.line->hide();
		}
		int at = 1; // after the dropdown
		for (const QString &id : ids) {
			const auto found = rows_.find(id);
			Row &row = found != rows_.end() ? found->second : addRow(id);
			choices_->addAction(row.choice);
			plugins_->insertWidget(at++, row.line);
		}
		order_ = ids;
	}
	for (const QString &id : order_)
		showRow(id);
	showList();
}

// A plugin's line in the dropdown (its name, what it does when pointed at, a note, and its buttons) and its line in the
// list, one line however many there are: its name and the gear. What it does folds out under the name.
ManagerDock::Row &ManagerDock::addRow(const QString &id)
{
	Row row{};
	auto *holder = new QWidget(choices_);
	holder->setObjectName(QStringLiteral("plugin:") + id);
	auto *grid = new QGridLayout(holder);
	grid->setContentsMargins(8, 3, 8, 3);
	grid->setColumnStretch(0, 1);
	row.name = plain(QString(), holder);
	row.name->setWordWrap(false);
	row.note = plain(QString(), holder);
	row.note->setObjectName(QStringLiteral("note"));
	row.note->setEnabled(false); // drawn softer
	row.update = new QPushButton(holder);
	row.update->setObjectName(QStringLiteral("update"));
	row.install = new QPushButton(holder);
	row.install->setObjectName(QStringLiteral("install"));
	grid->addWidget(row.name, 0, 0);
	grid->addWidget(row.update, 0, 1);
	grid->addWidget(row.install, 0, 2);
	grid->addWidget(row.note, 1, 0, 1, 3);
	row.choice = new QWidgetAction(this);
	row.choice->setDefaultWidget(holder);
	connect(row.install, &QPushButton::clicked, this, [this, id] {
		if (manager_.store().state(id).installed)
			manager_.store().uninstall(id);
		else
			manager_.store().install(id);
	});
	connect(row.update, &QPushButton::clicked, this, [this, id] { manager_.store().update(id); });

	QWidget *list = plugins_->parentWidget();
	row.line = new QWidget(list);
	auto *lineGrid = new QGridLayout(row.line);
	lineGrid->setContentsMargins(0, 0, 0, 0);
	lineGrid->setColumnStretch(0, 1);

	// The name opens and closes the description, with OBS's own arrow (the one that folds groups in Sources).
	// Checked means folded, as in OBS.
	row.title = new QCheckBox(row.line);
	row.title->setProperty("class", "indicator-expand");
	row.title->setChecked(true);
	QFont bold = row.title->font();
	bold.setBold(true);
	row.title->setFont(bold);
	row.about = plain(QString(), row.line);
	row.about->setContentsMargins(style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, row.title) +
					      style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, row.title),
				      0, 0, 0);
	row.about->hide();
	QLabel *about = row.about;
	connect(row.title, &QCheckBox::toggled, about, [about](bool folded) { about->setVisible(!folded); });

	row.gear = new QToolButton(row.line);
	row.gear->setProperty("class", "icon-gear"); // OBS's own gear, from its theme
	row.gear->setText(QStringLiteral("⚙")); // when there is no theme
	connect(row.gear, &QToolButton::clicked, this, [this, id] { openSettings(id); });

	lineGrid->addWidget(row.title, 0, 0);
	lineGrid->addWidget(row.gear, 0, 1);
	lineGrid->addWidget(row.about, 1, 0, 1, 2);
	row.line->hide();
	return rows_.emplace(id, row).first->second;
}

void ManagerDock::showRow(const QString &id)
{
	const auto found = rows_.find(id);
	if (found == rows_.end())
		return;
	Row &row = found->second;
	Store &store = manager_.store();
	Plugin *plugin = manager_.plugin(id);
	const Available *listed = store.find(id);

	// What it is called and does: as the plugin says it, or else as the list does.
	const QString name = plugin ? QString::fromUtf8(plugin->info->name) : listed ? listed->name : id;
	const QString about = plugin && plugin->info->description ? QString::fromUtf8(plugin->info->description)
			      : listed                             ? listed->description
								   : QString();
	row.name->setText(name);
	row.name->setToolTip(about);
	row.title->setText(name);
	row.about->setText(about);
	row.gear->setToolTip(text("Settings").arg(name));
	row.gear->setAccessibleName(row.gear->toolTip());

	const Store::State state = store.state(id);
	row.install->setText(text(state.busy ? "Downloading" : state.installed ? "Uninstall" : "Install"));
	row.install->setEnabled(!state.busy);
	row.update->setText(text("Update").arg(state.newVersion));
	row.update->setVisible(state.canUpdate);
	const QString note = !state.note.isEmpty()                  ? state.note
			     : state.change != Store::Change::None ? text("Restart")
								   : QString();
	row.note->setText(note);
	row.note->setVisible(!note.isEmpty());
	// The dropdown lays out its lines again (the next time it opens, or now if it is open): this one may have grown.
	row.choice->setVisible(false);
	row.choice->setVisible(true);

	// In the list while OBS runs it. Only a plugin that describes settings has a gear.
	const bool runs = plugin ? plugin->enabled : store.running(id);
	row.line->setVisible(runs);
	row.gear->setVisible(runs && plugin && plugin->info->get_properties);
	if (row.settings && !runs)
		row.settings->close();
	if (row.form && runs)
		row.form->reload();
	showList();
}

// What the dock says under the list: that it is empty, and that OBS has to restart. At the top of the dropdown: why
// there is no list of plugins.
void ManagerDock::showList()
{
	const bool any =
		std::any_of(rows_.begin(), rows_.end(), [](const auto &row) { return !row.second.line->isHidden(); });
	empty_->setVisible(!any);
	restart_->setVisible(manager_.store().restartNeeded());

	const Store::List list = manager_.store().list();
	if (auto *label = listState_->defaultWidget()->findChild<QLabel *>())
		label->setText(text(list == Store::List::Waiting ? "List.Waiting" : "List.Failed"));
	listState_->setVisible(list != Store::List::Ready);
}

void ManagerDock::openSettings(const QString &id)
{
	Plugin *plugin = manager_.plugin(id);
	if (!plugin)
		return;
	Row &row = rows_.at(id);
	if (!row.settings) {
		row.settings = new QDialog(this);
		row.settings->setWindowTitle(text("Settings").arg(QString::fromUtf8(plugin->info->name)));
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
