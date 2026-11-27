#include "ui/scenes/bind/param_facts.h"

#include "host/chains/params.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"

namespace deluge::ui::scenes::bind {

namespace {

bool is_engine(const project::PluginParam& p) {
	return p.target.space == project::ParamSpace::ENGINE;
}

bool is_chain(const project::PluginParam& p) {
	return p.plugin == project::OwnerPlugin::GLOBAL_EFFECTABLE;
}

} // namespace

const plugin::ParamInfo* info_of(const project::PluginParam& p) {
	if (!is_engine(p) || is_cable(p)) {
		return nullptr;
	}
	return is_chain(p) ? plugin::find_param(host::chains::kParams, p.target.id)
	                   : plugin::find_param(instruments::classic::kParams, p.target.id);
}

bool is_cable(const project::PluginParam& p) {
	return is_engine(p) && !is_chain(p) && instruments::classic::is_cable_depth(p.target.id);
}

bool is_patched(const project::PluginParam& p) {
	return is_engine(p) && !is_chain(p) && instruments::classic::is_patched(p.target.id);
}

bool is_bipolar(const project::PluginParam& p) {
	switch (p.target.space) {
	case project::ParamSpace::MIDI_CC:
		return false;
	case project::ParamSpace::EXPRESSION:
		return p.target.id == instruments::classic::kExpressionPitchBend;
	case project::ParamSpace::ENGINE:
		break;
	}
	if (is_cable(p)) {
		return true;
	}
	return is_chain(p) ? host::chains::is_bipolar(p.target.id) : instruments::classic::is_bipolar(p.target.id);
}

bool is_stutter_rate(const project::PluginParam& p) {
	if (!is_engine(p) || is_cable(p)) {
		return false;
	}
	return is_chain(p) ? host::chains::is_stutter_rate(p.target.id)
	                   : instruments::classic::is_stutter_rate(p.target.id);
}

bool is_arp_rhythm(const project::PluginParam& p) {
	return is_engine(p) && !is_chain(p) && instruments::classic::is_arp_rhythm(p.target.id);
}

} // namespace deluge::ui::scenes::bind
