#pragma once

/// @file
/// @brief The descriptor rows every Deluge host's owners have: the instrument's for a sound's params, the host
///        chain's for a kit's, an audio track's and the song's.

#include "plugin/types/param.h"
#include "plugin/types/plugin_id.h"
#include "project/param_address.h"

namespace deluge::scenes::bind {

/// @brief The descriptor row of the param @p target names on an owner of the plugin @p plugin.
/// @param plugin The owner's plugin.
/// @param target The param, in the owner's plugin's id space.
/// @return The row, or null for a non-engine param (a cable's depth, an expression dimension, a MIDI CC), another
///         plugin, or an id the plugin has no row for.
[[nodiscard]] const plugin::ParamInfo* deluge_param_info(plugin::PluginId plugin, const project::ParamTarget& target);

} // namespace deluge::scenes::bind
