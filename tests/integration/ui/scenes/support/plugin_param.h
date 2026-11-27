#pragma once

/// @file
/// @brief A param as the UI names it, built from the runtime's numbering that main's reference rows record.

#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_selection.h"
#include "modulation/params/param.h"
#include "project/owner_plugin.h"

#include <cstdint>

namespace deluge::ui::scenes::spec {

/// @brief The plugin param main's pair (@p kind, @p index) names: a global effectable's for an unpatched-global kind,
///        the instrument's for every other kind (a MIDI CC and an expression axis name no table).
/// @param kind  The runtime collection kind.
/// @param index The runtime index (a cable's packed word for a patch cable).
/// @return The plugin param; an ENGINE id 0 where the pair names nothing.
inline project::PluginParam plugin_param(plugin::ParamKind kind, int32_t index) {
	return bridge::pluginParamOf(kind, index)
	    .value_or(project::PluginParam{.plugin = kind == plugin::ParamKind::UNPATCHED_GLOBAL
	                                                 ? project::OwnerPlugin::GLOBAL_EFFECTABLE
	                                                 : project::OwnerPlugin::INSTRUMENT});
}

/// @brief @p param in the runtime's numbering, as main's reference rows record it.
inline bridge::RuntimeParam runtime_pair(const project::PluginParam& param) {
	return bridge::runtimeParamOf(param.plugin, param.target).value_or(bridge::RuntimeParam{});
}

} // namespace deluge::ui::scenes::spec
