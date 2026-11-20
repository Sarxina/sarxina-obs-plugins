#include "topics-dialog.hpp"

#include <obs-module.h>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace sarxina::wheelofyap {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QListWidgetItem *row(const Topic &topic)
{
	auto *item = new QListWidgetItem(topic.label);
	item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable |
		       Qt::ItemIsDragEnabled);
	item->setCheckState(topic.on ? Qt::Checked : Qt::Unchecked);
	return item;
}

} // namespace

TopicsDialog::TopicsDialog(const std::vector<Topic> &topics, std::function<void(std::vector<Topic>)> saved,
			   QWidget *parent)
	: QDialog(parent), saved_(std::move(saved))
{
	setWindowTitle(text("Topics.Title"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(520, 680);

	list_ = new QListWidget(this);
	list_->setDragDropMode(QAbstractItemView::InternalMove);
	list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
	list_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
			       QAbstractItemView::SelectedClicked);
	for (const Topic &topic : topics)
		list_->addItem(row(topic));
	connect(list_, &QListWidget::itemChanged, this, [this] { count(); });
	connect(list_->model(), &QAbstractItemModel::rowsMoved, this, [this] { count(); });

	auto *buttons = new QHBoxLayout();
	const auto button = [this, buttons](const char *key, auto pressed) {
		auto *b = new QPushButton(text(key), this);
		b->setAutoDefault(false);
		connect(b, &QPushButton::clicked, this, pressed);
		buttons->addWidget(b);
	};
	button("Topics.Add", [this] { add(); });
	button("Topics.Remove", [this] { remove(); });
	button("Topics.Up", [this] { move(-1); });
	button("Topics.Down", [this] { move(1); });
	buttons->addStretch();
	count_ = new QLabel(this);
	buttons->addWidget(count_);

	auto *done = new QDialogButtonBox(this);
	done->addButton(text("Topics.Save"), QDialogButtonBox::AcceptRole);
	done->addButton(text("Topics.Cancel"), QDialogButtonBox::RejectRole);
	connect(done, &QDialogButtonBox::accepted, this, [this] {
		if (saved_)
			saved_(this->topics());
		accept();
	});
	connect(done, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(list_);
	layout->addLayout(buttons);
	layout->addWidget(done);
	count();
}

void TopicsDialog::add()
{
	const int at = list_->currentRow() >= 0 ? list_->currentRow() + 1 : list_->count();
	QListWidgetItem *item = row({text("Topics.New"), true});
	list_->insertItem(at, item);
	list_->setCurrentItem(item);
	list_->editItem(item);
	count();
}

void TopicsDialog::remove()
{
	for (QListWidgetItem *item : list_->selectedItems())
		delete item;
	count();
}

void TopicsDialog::move(int by)
{
	const int from = list_->currentRow();
	const int to = from + by;
	if (from < 0 || to < 0 || to >= list_->count())
		return;
	QListWidgetItem *item = list_->takeItem(from);
	list_->insertItem(to, item);
	list_->setCurrentItem(item);
}

void TopicsDialog::count()
{
	int on = 0;
	for (int i = 0; i < list_->count(); i++)
		on += list_->item(i)->checkState() == Qt::Checked ? 1 : 0;
	count_->setText(text("Topics.Count").arg(list_->count()).arg(on));
}

std::vector<Topic> TopicsDialog::topics() const
{
	std::vector<Topic> topics;
	for (int i = 0; i < list_->count(); i++) {
		const QListWidgetItem *item = list_->item(i);
		const QString label = topicText(item->text());
		if (!label.isEmpty())
			topics.push_back({label, item->checkState() == Qt::Checked});
	}
	return topics;
}

} // namespace sarxina::wheelofyap
