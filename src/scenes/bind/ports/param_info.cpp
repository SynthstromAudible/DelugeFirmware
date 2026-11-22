#include "scenes/bind/ports/param_info.h"

#include "effects/host_chain/params.h"
#include "instruments/classic/params.h"

namespace deluge::scenes::bind {

const plugin::ParamInfo* deluge_param_info(plugin::PluginId plugin, const project::ParamTarget& target) {
	if (target.space != project::ParamSpace::ENGINE) {
		return nullptr;
	}
	if (plugin == plugin::kDelugeInstrument) {
		return plugin::find_param(instruments::classic::kParams, target.id);
	}
	return plugin == plugin::kGlobalEffectable ? plugin::find_param(host_chain::kParams, target.id) : nullptr;
}

} // namespace deluge::scenes::bind
