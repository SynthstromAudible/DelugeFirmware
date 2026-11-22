#include "scenes/bind/ports/param_info.h"

#include "host/chains/params.h"
#include "instruments/classic/params.h"

namespace deluge::scenes::bind {

const plugin::ParamInfo* deluge_param_info(plugin::PluginId plugin, const project::ParamTarget& target) {
	if (target.space != project::ParamSpace::ENGINE) {
		return nullptr;
	}
	if (plugin == plugin::kDelugeInstrument) {
		return plugin::find_param(instruments::classic::kParams, target.id);
	}
	return plugin == plugin::kGlobalEffectable ? plugin::find_param(host::chains::kParams, target.id) : nullptr;
}

} // namespace deluge::scenes::bind
