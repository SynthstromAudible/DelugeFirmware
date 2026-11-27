#pragma once

/// @file
/// @brief The plugin nodes of the model's records: an instrument slot's state and a chain, each written by its
/// plugin's codec as it writes a legacy element today, inside a versioned node (serialization/schema/plugin_node.h).

#include "project/controller_mapping.h"
#include "serialization/schema/descriptor.h"

#include <cstdint>

namespace deluge::project::schema {

namespace sch = deluge::serialization::schema;

/// @brief The walk's user data for the model's schemas.
struct CleanContext {
	const CodecPlugins* plugins;
	/// The song's input tick magnitude: the sync values of a chain's entries are relative to it.
	int32_t tick_magnitude;
};

/// The node version the instrument's entries are written in: today's sound element's form.
inline constexpr uint32_t kInstrumentStateVersion = 1;
/// The node version a chain's entries are written in: today's element form.
inline constexpr uint32_t kChainStateVersion = 1;

void write_instrument_state(const void* member, sch::Writer& out, const sch::WalkContext& context);
sch::PluginRead read_instrument_state(void* member, const serialization::Value& node, sch::WalkContext& context);
void write_track_chain(const void* member, sch::Writer& out, const sch::WalkContext& context);
sch::PluginRead read_track_chain(void* member, const serialization::Value& node, sch::WalkContext& context);
void write_master_chain(const void* member, sch::Writer& out, const sch::WalkContext& context);
sch::PluginRead read_master_chain(void* member, const serialization::Value& node, sch::WalkContext& context);

/// @brief An instrument slot's state (the member is the InstrumentSlot): read only when the slot's plugin is the
/// Deluge instrument and a decoder is registered; any other is kept.
inline constexpr sch::PluginOps kInstrumentStateOps{"deluge.instrument", kInstrumentStateVersion,
                                                    &write_instrument_state, &read_instrument_state};
/// @brief A kit's or audio track's chain (the member is the FxChain).
inline constexpr sch::PluginOps kTrackChainOps{"deluge.effects/track", kChainStateVersion, &write_track_chain,
                                               &read_track_chain};
/// @brief The song master's chain (the member is the FxChain).
inline constexpr sch::PluginOps kMasterChainOps{"deluge.effects/master", kChainStateVersion, &write_master_chain,
                                                &read_master_chain};

} // namespace deluge::project::schema
