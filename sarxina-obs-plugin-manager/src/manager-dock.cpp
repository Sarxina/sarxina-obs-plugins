#include "manager-dock.hpp"

#include "properties-form.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
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

	layout->addWidget(addManagerLine(content));

	// Connections
	auto *connections = new QGroupBox(text("Connections"), content);
	auto *grid = new QGridLayout(connections);
	grid->setColumnStretch(1, 1);

	// Twitch: the one connection every plugin's chat and channel points go through, which the streamer makes here.
	twitchState_ = plain(QString(), connections);
	twitchState_->setObjectName(QStringLiteral("twitch-state"));
	twitchButton_ = new QPushButton(connections);
	twitchButton_->setObjectName(QStringLiteral("twitch-button"));
	chatState_ = plain(QString(), connections);
	chatState_->setObjectName(QStringLiteral("twitch-chat"));
	auto *twitch = new QHBoxLayout; // the words take the whole line when there is no button
	twitch->addWidget(twitchState_, 1);
	twitch->addWidget(twitchButton_);
	grid->addWidget(plain(text("Twitch"), connections), 0, 0);
	grid->addLayout(twitch, 0, 1, 1, 2);
	grid->addWidget(chatState_, 1, 1, 1, 2);

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
	connect(&manager.twitchApi(), &TwitchApi::stateChanged, this, &ManagerDock::showTwitch);
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
	auto *choices = new QWidgetAction(this);
	choices->setDefaultWidget(addChoices());
	choices_->addAction(choices);
	connect(choices_, &QMenu::aboutToShow, this, [this] {
		if (manager_.store().list() == Store::List::Failed)
			manager_.store().refresh();
		fitChoices();
	});
	// Once it shows, OBS's theme has dressed every line: sized again to them.
	connect(choices_, &QMenu::aboutToShow, this, [this] { QTimer::singleShot(0, this, &ManagerDock::fitChoices); });

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
	connect(&manager.store(), &Store::managerChanged, this, [this] {
		showManager();
		showList();
	});
	showManager();
}

// ---- the manager itself ---------------------------------------------------------------------------------------------

// The line at the top that offers a newer manager, and says how updating it goes.
QWidget *ManagerDock::addManagerLine(QWidget *parent)
{
	managerLine_ = new QWidget(parent);
	managerLine_->setObjectName(QStringLiteral("manager"));
	auto *line = new QHBoxLayout(managerLine_);
	line->setContentsMargins(4, 0, 4, 0);
	managerState_ = plain(QString(), managerLine_);
	managerState_->setObjectName(QStringLiteral("manager-state"));
	managerArrow_ = upArrow(managerLine_);
	connect(managerArrow_, &QToolButton::clicked, this, [this] { manager_.store().updateManager(); });
	line->addWidget(managerState_, 1);
	line->addWidget(managerArrow_);
	return managerLine_;
}

void ManagerDock::showManager()
{
	const Store::State state = manager_.store().managerState();
	QString says;
	if (state.busy)
		says = text("Downloading");
	else if (!state.note.isEmpty())
		says = state.note;
	else if (state.change != Store::Change::None)
		says = text("Restart");
	else if (state.canUpdate)
		says = text("Manager").arg(state.newVersion);
	managerState_->setText(says);
	managerArrow_->setText(text("Update").arg(state.newVersion));
	managerArrow_->setToolTip(managerArrow_->text());
	managerArrow_->setAccessibleName(managerArrow_->text());
	managerArrow_->setVisible(state.canUpdate);
	managerLine_->setVisible(!says.isEmpty());
}

// ---- connections ----------------------------------------------------------------------------------------------------

void ManagerDock::pressTwitch()
{
	TwitchApi &api = manager_.twitchApi();
	switch (api.state()) {
	case TwitchApi::State::Off:
	case TwitchApi::State::Reconnect: api.connectTwitch(); break;
	case TwitchApi::State::Waiting: api.openActivation(); break;
	case TwitchApi::State::Ready: api.disconnectTwitch(); break;
	default: break;
	}
}

void ManagerDock::showTwitch()
{
	const TwitchApi &api = manager_.twitchApi();
	const auto why = [&api] {
		switch (api.why()) {
		case TwitchApi::Why::Unreachable: return text("Twitch.Unreachable");
		case TwitchApi::Why::Expired: return text("Twitch.Expired");
		case TwitchApi::Why::Denied: return text("Twitch.Denied");
		case TwitchApi::Why::Refused: return text("Twitch.Refused");
		case TwitchApi::Why::Scope: return text("Twitch.Scope");
		case TwitchApi::Why::SignedOut: return text("Twitch.SignedOut");
		case TwitchApi::Why::Nothing: break;
		}
		return text("Off");
	};
	const char *button = nullptr;
	switch (api.state()) {
	case TwitchApi::State::NoApp: twitchState_->setText(text("Twitch.NoApp")); break;
	case TwitchApi::State::Off:
		twitchState_->setText(why());
		button = "Connect";
		break;
	case TwitchApi::State::Connecting: twitchState_->setText(text("Twitch.Connecting")); break;
	case TwitchApi::State::Waiting:
		twitchState_->setText(text("Twitch.Code").arg(api.userCode()));
		button = "Open";
		break;
	case TwitchApi::State::Ready:
		twitchState_->setText(text("Twitch.Connected").arg(api.login()));
		button = "Disconnect";
		break;
	case TwitchApi::State::Reconnect:
		twitchState_->setText(text("Twitch.Reconnect").arg(why()));
		button = "Reconnect";
		break;
	}
	// The code can be copied.
	twitchState_->setTextInteractionFlags(api.state() == TwitchApi::State::Waiting ? Qt::TextSelectableByMouse
										   : Qt::NoTextInteraction);
	twitchButton_->setText(button ? text(button) : QString());
	twitchButton_->setVisible(button != nullptr);

	// Under it, the chat of the account connected.
	const QString channel = manager_.twitch().channel();
	QString chat;
	switch (manager_.twitch().state()) {
	case TwitchChat::State::Off: break;
	case TwitchChat::State::Connecting: chat = text("Chat.Connecting").arg(channel); break;
	case TwitchChat::State::Reading: chat = text("Chat.Reading").arg(channel); break;
	case TwitchChat::State::Retrying: chat = text("Chat.Retrying"); break;
	}
	chatState_->setText(chat);
	chatState_->setVisible(!chat.isEmpty());
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

// The dropdown's lines, in a list that scrolls when there are more than fit.
QWidget *ManagerDock::addChoices()
{
	choicesView_ = new QScrollArea(choices_);
	choicesView_->setObjectName(QStringLiteral("choices"));
	// OBS's theme pads and frames scroll areas; inside the dropdown that would only cut the lines short.
	choicesView_->setStyleSheet(QStringLiteral("QScrollArea#choices { border: none; padding: 0px; margin: 0px; }"));
	choicesView_->setFrameShape(QFrame::NoFrame);
	choicesView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	choicesView_->setWidgetResizable(true);
	auto *rows = new QWidget(choicesView_);
	choiceRows_ = new QVBoxLayout(rows);
	choiceRows_->setContentsMargins(0, 2, 0, 2);
	choiceRows_->setSpacing(0);
	choiceRows_->addStretch(1);
	choicesView_->setWidget(rows);
	return choicesView_;
}

// Sizes the dropdown's list to its lines, up to kShownChoices of them; past that it scrolls.
void ManagerDock::fitChoices()
{
	constexpr int kShownChoices = 7;
	QWidget *rows = choicesView_->widget();
	for (QWidget *part : rows->findChildren<QWidget *>())
		part->ensurePolished(); // measured in OBS's theme
	rows->layout()->activate();
	// The list lays its lines out as slim as they go, so they are measured that way.
	int line = 0; // a line without a note
	for (const QString &id : order_) {
		const int height = rows_.at(id).choice->minimumSizeHint().height();
		line = line == 0 ? height : std::min(line, height);
	}
	const QMargins margins = choiceRows_->contentsMargins();
	const int whole = rows->minimumSizeHint().height();
	const int cap = line * kShownChoices + margins.top() + margins.bottom();
	const bool scrolls = whole > cap && static_cast<int>(order_.size()) > kShownChoices;
	const int bar = scrolls ? choicesView_->verticalScrollBar()->sizeHint().width() : 0;
	choicesView_->setVerticalScrollBarPolicy(scrolls ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
	choicesView_->setFixedSize(rows->sizeHint().width() + bar, scrolls ? cap : whole);
	choicesView_->setVisible(!order_.empty());
	if (choices_->isVisible())
		choices_->adjustSize();
}

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
			choiceRows_->removeWidget(row.choice);
			plugins_->removeWidget(row.line);
			row.line->hide();
		}
		int choiceAt = 0;
		int lineAt = 1; // after the dropdown
		for (const QString &id : ids) {
			const auto found = rows_.find(id);
			Row &row = found != rows_.end() ? found->second : addRow(id);
			choiceRows_->insertWidget(choiceAt++, row.choice);
			plugins_->insertWidget(lineAt++, row.line);
		}
		order_ = ids;
	}
	for (const QString &id : order_)
		showRow(id);
	showList();
	fitChoices();
}

// A plugin's line in the dropdown and its line in the list.
ManagerDock::Row &ManagerDock::addRow(const QString &id)
{
	Row row{};
	row.choice = addChoice(id, row);
	row.line = addLine(id, row);
	return rows_.emplace(id, row).first->second;
}

// A plugin's slim line in the dropdown: its name (what it does when pointed at), the up arrow when there is a newer
// version, its button, and a note under them when there is one.
QWidget *ManagerDock::addChoice(const QString &id, Row &row)
{
	auto *holder = new QWidget(choicesView_->widget());
	holder->setObjectName(QStringLiteral("plugin:") + id);
	auto *grid = new QGridLayout(holder);
	grid->setContentsMargins(8, 1, 6, 1);
	grid->setHorizontalSpacing(4);
	grid->setVerticalSpacing(0);
	grid->setColumnStretch(0, 1);
	row.name = plain(QString(), holder);
	row.name->setWordWrap(false);
	row.note = plain(QString(), holder);
	row.note->setObjectName(QStringLiteral("note"));
	row.note->setEnabled(false); // drawn softer
	row.update = upArrow(holder);
	connect(row.update, &QToolButton::clicked, this, [this, id] { manager_.store().update(id); });
	row.install = new QToolButton(holder);
	row.install->setObjectName(QStringLiteral("install"));
	row.install->setToolButtonStyle(Qt::ToolButtonTextOnly);
	row.install->setStyleSheet(QStringLiteral("padding: 1px 8px; min-height: 0px;")); // slim, in the theme's colours
	connect(row.install, &QToolButton::clicked, this, [this, id] {
		if (manager_.store().state(id).installed)
			manager_.store().uninstall(id);
		else
			manager_.store().install(id);
	});
	grid->addWidget(row.name, 0, 0);
	grid->addWidget(row.update, 0, 1);
	grid->addWidget(row.install, 0, 2);
	grid->addWidget(row.note, 1, 0, 1, 3);
	return holder;
}

// A plugin's line in the list, one line however many there are: its name and the gear. What it does folds out under
// the name.
QWidget *ManagerDock::addLine(const QString &id, Row &row)
{
	auto *line = new QWidget(plugins_->parentWidget());
	auto *grid = new QGridLayout(line);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setVerticalSpacing(0);
	grid->setColumnStretch(0, 1);

	// The name opens and closes the description, with OBS's own arrow (the one that folds groups in Sources).
	// Checked means folded, as in OBS.
	row.title = new QCheckBox(line);
	row.title->setProperty("class", "indicator-expand");
	row.title->setChecked(true);
	QFont bold = row.title->font();
	bold.setBold(true);
	row.title->setFont(bold);
	row.about = plain(QString(), line);
	row.about->setContentsMargins(style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, row.title) +
					      style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, row.title),
				      0, 0, 0);
	row.about->hide();
	QLabel *about = row.about;
	connect(row.title, &QCheckBox::toggled, about, [about](bool folded) { about->setVisible(!folded); });

	row.gear = new QToolButton(line);
	row.gear->setProperty("class", "icon-gear"); // OBS's own gear, from its theme
	row.gear->setText(QStringLiteral("⚙")); // when there is no theme
	connect(row.gear, &QToolButton::clicked, this, [this, id] { openSettings(id); });

	row.lineUpdate = upArrow(line);
	connect(row.lineUpdate, &QToolButton::clicked, this, [this, id] { manager_.store().update(id); });

	grid->addWidget(row.title, 0, 0);
	grid->addWidget(row.lineUpdate, 0, 1);
	grid->addWidget(row.gear, 0, 2);
	grid->addWidget(row.about, 1, 0, 1, 3);
	line->hide();
	return line;
}

namespace {

// An up arrow in the colour of the theme's text, so that it reads on light and dark themes alike.
QIcon upArrowIcon(const QColor &colour)
{
	QPixmap picture(32, 32);
	picture.fill(Qt::transparent);
	QPainter painter(&picture);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(colour, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.drawLine(QPointF(16, 26), QPointF(16, 8));
	painter.drawPolyline(QPolygonF({QPointF(8, 15), QPointF(16, 7), QPointF(24, 15)}));
	return QIcon(picture);
}

} // namespace

// The small up arrow that offers a newer version, hidden until there is one.
QToolButton *ManagerDock::upArrow(QWidget *parent)
{
	auto *arrow = new QToolButton(parent);
	arrow->setObjectName(QStringLiteral("update"));
	arrow->setIcon(upArrowIcon(arrow->palette().color(QPalette::ButtonText)));
	arrow->setToolButtonStyle(Qt::ToolButtonIconOnly);
	arrow->setAutoRaise(true);
	arrow->hide();
	return arrow;
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
	// What it does: as the list says it, the newest words, or else as the plugin does.
	const QString about = listed ? listed->description
			      : plugin && plugin->info->description ? QString::fromUtf8(plugin->info->description)
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
	for (QToolButton *arrow : {row.update, row.lineUpdate}) {
		arrow->setText(text("Update").arg(state.newVersion)); // read out, and shown when it is pointed at
		arrow->setToolTip(arrow->text());
		arrow->setAccessibleName(arrow->text());
		arrow->setVisible(state.canUpdate && !state.busy);
	}
	const QString note = !state.note.isEmpty()                  ? state.note
			     : state.change != Store::Change::None ? text("Restart")
								   : QString();
	row.note->setText(note);
	row.note->setVisible(!note.isEmpty());
	fitChoices(); // this line may have grown or shrunk

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
