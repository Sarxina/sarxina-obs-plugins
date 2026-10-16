// A plugin's settings, drawn from how the plugin describes them: OBS's own properties, the same way a source describes
// its settings. Every change goes straight to the plugin. It draws check boxes, whole and decimal numbers, text and
// lists; a property of any other kind is left out.
#pragma once

#include "plugin.hpp"

#include <obs.hpp>

#include <QWidget>

namespace sarxina {

class PropertiesForm : public QWidget {
	Q_OBJECT

public:
	PropertiesForm(Plugin &plugin, QWidget *parent);
	~PropertiesForm() override;

	// Asks the plugin for its settings again, and shows them.
	void reload();

private:
	void clear();
	void build();
	QWidget *field(obs_property_t *property);
	void changed(obs_property_t *property);

	Plugin &plugin_;
	obs_properties_t *properties_ = nullptr;
	OBSDataAutoRelease settings_;
	QWidget *fields_ = nullptr;
};

} // namespace sarxina
