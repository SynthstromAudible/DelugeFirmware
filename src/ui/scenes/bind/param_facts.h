#pragma once

/// @file
/// @brief What the UI reads about a param it names by its plugin and target: its row, and the facts the knobs, the
///        popup and the menu show it by.

#include "plugin/types/param.h"
#include "project/owner_plugin.h"
#include "project/param_address.h"

namespace deluge::ui::scenes::bind {

/// @brief The descriptor row of @p p in its plugin's table.
/// @param p A param.
/// @return The row; null outside the ENGINE space, for a cable's depth, and for an id the table lacks.
[[nodiscard]] const plugin::ParamInfo* info_of(const project::PluginParam& p);

/// @brief Is @p p a cable's depth? Only the instrument has cables.
[[nodiscard]] bool is_cable(const project::PluginParam& p);

/// @brief Is @p p one of a sound's patched params: the ones a cable reaches and a source pad patches?
[[nodiscard]] bool is_patched(const project::PluginParam& p);

/// @brief Does @p p's knob swing both ways around a middle value? Every cable's depth does, as does the pitch-bend
///        expression axis and any param whose row is flagged @c ParamFlag::CENTRED.
[[nodiscard]] bool is_bipolar(const project::PluginParam& p);

/// @brief Is @p p the stutter rate, of a sound or of a chain?
[[nodiscard]] bool is_stutter_rate(const project::PluginParam& p);

/// @brief Is @p p the arpeggiator's rhythm, a sound's param?
[[nodiscard]] bool is_arp_rhythm(const project::PluginParam& p);

/// @brief The param @p address names, read in @p plugin's table.
/// @param plugin Whose table the address's ENGINE id is in: its owner's plugin.
/// @param address The param's address.
/// @return The plugin param.
[[nodiscard]] constexpr project::PluginParam plugin_param_of(project::OwnerPlugin plugin,
                                                             const project::ParamAddress& address) {
	return {.plugin = plugin, .target = address.target()};
}

} // namespace deluge::ui::scenes::bind
