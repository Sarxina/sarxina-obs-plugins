// A plugin that handed itself to the manager (see sarxina-obs-plugin-manager.h).
#pragma once

#include "sarxina-obs-plugin-manager.h"

namespace sarxina {

struct Plugin {
	const sarxina_plugin_info *info;
	bool enabled = true;
};

} // namespace sarxina
