#include "properties-form.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>

namespace sarxina {

namespace {

QLabel *plain(const char *text, QWidget *parent)
{
	auto *label = new QLabel(QString::fromUtf8(text), parent);
	label->setTextFormat(Qt::PlainText);
	return label;
}

} // namespace

PropertiesForm::PropertiesForm(Plugin &plugin, QWidget *parent) : QWidget(parent), plugin_(plugin)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	reload();
}

PropertiesForm::~PropertiesForm()
{
	obs_properties_destroy(properties_);
}

void PropertiesForm::reload()
{
	const sarxina_plugin_info &info = *plugin_.info;
	clear();
	obs_properties_destroy(properties_);
	properties_ = info.get_properties ? info.get_properties(info.data) : nullptr;
	settings_ = obs_data_create();
	if (info.get_settings)
		info.get_settings(info.data, settings_);
	if (properties_)
		obs_properties_apply_settings(properties_, settings_); // lets the plugin show or hide what depends on what
	build();
}

// The old fields stop reporting changes at once, and go once Qt is done with them: this can run while one of them is
// reporting a change.
void PropertiesForm::clear()
{
	if (!fields_)
		return;
	for (QWidget *widget : fields_->findChildren<QWidget *>())
		disconnect(widget, nullptr, this, nullptr);
	fields_->deleteLater();
	fields_ = nullptr;
}

void PropertiesForm::build()
{
	clear();
	fields_ = new QWidget(this);
	auto *form = new QFormLayout(fields_);
	form->setContentsMargins(0, 0, 0, 0);
	for (obs_property_t *property = properties_ ? obs_properties_first(properties_) : nullptr; property;
	     obs_property_next(&property)) {
		if (!obs_property_visible(property))
			continue;
		QWidget *widget = field(property);
		if (!widget)
			continue;
		widget->setEnabled(obs_property_enabled(property));
		widget->setToolTip(QString::fromUtf8(obs_property_long_description(property)));
		const char *label = obs_property_description(property);
		if (obs_property_get_type(property) == OBS_PROPERTY_BOOL)
			form->addRow(widget);
		else
			form->addRow(plain(label, fields_), widget);
	}
	layout()->addWidget(fields_);
	setVisible(form->rowCount() > 0);
}

QWidget *PropertiesForm::field(obs_property_t *property)
{
	const char *name = obs_property_name(property);
	switch (obs_property_get_type(property)) {
	case OBS_PROPERTY_BOOL: {
		auto *box = new QCheckBox(QString::fromUtf8(obs_property_description(property)), fields_);
		box->setChecked(obs_data_get_bool(settings_, name));
		connect(box, &QCheckBox::toggled, this, [this, property, name](bool on) {
			obs_data_set_bool(settings_, name, on);
			changed(property);
		});
		return box;
	}
	case OBS_PROPERTY_INT: {
		auto *box = new QSpinBox(fields_);
		box->setRange(obs_property_int_min(property), obs_property_int_max(property));
		box->setSingleStep(obs_property_int_step(property));
		box->setSuffix(QString::fromUtf8(obs_property_int_suffix(property)));
		box->setValue(static_cast<int>(obs_data_get_int(settings_, name)));
		connect(box, &QSpinBox::valueChanged, this, [this, property, name](int value) {
			obs_data_set_int(settings_, name, value);
			changed(property);
		});
		return box;
	}
	case OBS_PROPERTY_FLOAT: {
		auto *box = new QDoubleSpinBox(fields_);
		const double step = obs_property_float_step(property);
		box->setDecimals(step > 0 ? std::max(0, static_cast<int>(std::ceil(-std::log10(step)))) : 2);
		box->setRange(obs_property_float_min(property), obs_property_float_max(property));
		box->setSingleStep(step);
		box->setSuffix(QString::fromUtf8(obs_property_float_suffix(property)));
		box->setValue(obs_data_get_double(settings_, name));
		connect(box, &QDoubleSpinBox::valueChanged, this, [this, property, name](double value) {
			obs_data_set_double(settings_, name, value);
			changed(property);
		});
		return box;
	}
	case OBS_PROPERTY_TEXT: {
		const QString text = QString::fromUtf8(obs_data_get_string(settings_, name));
		if (obs_property_text_type(property) == OBS_TEXT_INFO) {
			QLabel *note = plain(obs_property_description(property), fields_);
			note->setWordWrap(true);
			return note;
		}
		auto *edit = new QLineEdit(text, fields_);
		if (obs_property_text_type(property) == OBS_TEXT_PASSWORD)
			edit->setEchoMode(QLineEdit::Password);
		connect(edit, &QLineEdit::editingFinished, this, [this, edit, property, name] {
			obs_data_set_string(settings_, name, edit->text().toUtf8().constData());
			changed(property);
		});
		return edit;
	}
	case OBS_PROPERTY_LIST: {
		auto *box = new QComboBox(fields_);
		const obs_combo_format format = obs_property_list_format(property);
		for (size_t i = 0, count = obs_property_list_item_count(property); i < count; i++) {
			const QString label = QString::fromUtf8(obs_property_list_item_name(property, i));
			bool current = false;
			switch (format) {
			case OBS_COMBO_FORMAT_INT:
				current = obs_property_list_item_int(property, i) == obs_data_get_int(settings_, name);
				break;
			case OBS_COMBO_FORMAT_FLOAT:
				current = obs_property_list_item_float(property, i) == obs_data_get_double(settings_, name);
				break;
			case OBS_COMBO_FORMAT_STRING:
				current = std::strcmp(obs_property_list_item_string(property, i),
						      obs_data_get_string(settings_, name)) == 0;
				break;
			case OBS_COMBO_FORMAT_BOOL:
				current = obs_property_list_item_bool(property, i) == obs_data_get_bool(settings_, name);
				break;
			default:
				break;
			}
			box->addItem(label);
			if (current)
				box->setCurrentIndex(static_cast<int>(i));
		}
		connect(box, &QComboBox::currentIndexChanged, this, [this, property, name, format](int index) {
			const auto i = static_cast<size_t>(index);
			switch (format) {
			case OBS_COMBO_FORMAT_INT:
				obs_data_set_int(settings_, name, obs_property_list_item_int(property, i));
				break;
			case OBS_COMBO_FORMAT_FLOAT:
				obs_data_set_double(settings_, name, obs_property_list_item_float(property, i));
				break;
			case OBS_COMBO_FORMAT_STRING:
				obs_data_set_string(settings_, name, obs_property_list_item_string(property, i));
				break;
			case OBS_COMBO_FORMAT_BOOL:
				obs_data_set_bool(settings_, name, obs_property_list_item_bool(property, i));
				break;
			default:
				return;
			}
			changed(property);
		});
		return box;
	}
	default:
		return nullptr;
	}
}

void PropertiesForm::changed(obs_property_t *property)
{
	const bool redraw = obs_property_modified(property, settings_);
	const sarxina_plugin_info &info = *plugin_.info;
	if (info.update)
		info.update(info.data, settings_);
	if (redraw)
		build();
}

} // namespace sarxina
