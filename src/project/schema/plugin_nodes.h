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

/// @brief Whether an instrument slot holds a state of its own.
/// @param member The InstrumentSlot.
/// @return True when its state handle holds one.
[[nodiscard]] bool instrument_has_state(const void* member);
/// @brief Write an instrument slot's state as a node, through the registered instrument codec; `null` for a slot
///        without one, or with no codec registered.
/// @param member  The InstrumentSlot.
/// @param out     Where to write.
/// @param context The walk's context, whose user data is a CleanContext.
void write_instrument_state(const void* member, sch::Writer& out, const sch::WalkContext& context);
/// @brief Read an instrument slot's state from its node: decoded when the slot's plugin is the Deluge instrument
///        and its codec is registered, kept otherwise. The slot's plugin id is read before its state, whatever the
///        document's key order, because the walk reads plugin fields last.
/// @param member  The InstrumentSlot.
/// @param node    The node, or `null`.
/// @param context The walk's context, whose user data is a CleanContext.
/// @return What the read made of the node.
[[nodiscard]] sch::PluginRead read_instrument_state(void* member, const serialization::Value& node,
                                                    sch::WalkContext& context);
/// @brief Whether a chain holds a state of its own.
/// @param member The FxChain.
/// @return True when its effects handle holds one. A decoded chain always does; one without is the default chain.
[[nodiscard]] bool chain_has_state(const void* member);
/// @brief Write a kit's or audio track's chain as a node, through the registered chain codec; `null` for the
///        default chain, or with no codec registered.
/// @param member  The FxChain.
/// @param out     Where to write.
/// @param context The walk's context, whose user data is a CleanContext.
void write_track_chain(const void* member, sch::Writer& out, const sch::WalkContext& context);
/// @brief Read a kit's or audio track's chain from its node; `null` reads as the default chain.
/// @param member  The FxChain.
/// @param node    The node, or `null`.
/// @param context The walk's context, whose user data is a CleanContext.
/// @return What the read made of the node.
[[nodiscard]] sch::PluginRead read_track_chain(void* member, const serialization::Value& node,
                                               sch::WalkContext& context);
/// @brief Write the song master's chain as a node (as write_track_chain).
/// @param member  The FxChain.
/// @param out     Where to write.
/// @param context The walk's context, whose user data is a CleanContext.
void write_master_chain(const void* member, sch::Writer& out, const sch::WalkContext& context);
/// @brief Read the song master's chain from its node (as read_track_chain).
/// @param member  The FxChain.
/// @param node    The node, or `null`.
/// @param context The walk's context, whose user data is a CleanContext.
/// @return What the read made of the node.
[[nodiscard]] sch::PluginRead read_master_chain(void* member, const serialization::Value& node,
                                                sch::WalkContext& context);

/// @brief An instrument slot's state (the member is the InstrumentSlot).
inline constexpr sch::PluginOps kInstrumentStateOps{.name = "deluge.instrument",
                                                    .version = kInstrumentStateVersion,
                                                    .has_state = &instrument_has_state,
                                                    .write = &write_instrument_state,
                                                    .read = &read_instrument_state};
/// @brief A kit's or audio track's chain (the member is the FxChain).
inline constexpr sch::PluginOps kTrackChainOps{.name = "deluge.effects/track",
                                               .version = kChainStateVersion,
                                               .has_state = &chain_has_state,
                                               .write = &write_track_chain,
                                               .read = &read_track_chain};
/// @brief The song master's chain (the member is the FxChain).
inline constexpr sch::PluginOps kMasterChainOps{.name = "deluge.effects/master",
                                                .version = kChainStateVersion,
                                                .has_state = &chain_has_state,
                                                .write = &write_master_chain,
                                                .read = &read_master_chain};

} // namespace deluge::project::schema
