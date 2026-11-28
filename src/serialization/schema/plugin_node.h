#pragma once

/// @file
/// @brief A plugin node: a plugin's state as its plugin's codec writes it, with the version of that form and the tick
/// magnitude its sync values are relative to: `{"version": n, "tickMagnitude": m, "entries": {…}}`.

#include "serialization/schema/writer.h"
#include "serialization/value.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::serialization::schema {

inline constexpr std::string_view kVersionKey = "version";
inline constexpr std::string_view kTickMagnitudeKey = "tickMagnitude";
inline constexpr std::string_view kEntriesKey = "entries";

/// @brief A plugin node read from a document.
struct PluginNode {
	uint32_t version;
	int32_t tick_magnitude;
	const Value* entries; ///< a Map, inside the node it was read from
};

/// @brief Write a plugin node.
/// @param out            Where to write it.
/// @param version        The version of the entries' form.
/// @param tick_magnitude The tick magnitude the entries' sync values are relative to.
/// @param entries        The plugin's state as its codec wrote it.
void write_plugin_node(Writer& out, uint32_t version, int32_t tick_magnitude, const Value& entries);

/// @brief Read @p node as a plugin node.
/// @param node A value read from a document.
/// @return The node; nullopt unless it is a Map with a whole-number version and tick magnitude and a Map of entries.
[[nodiscard]] std::optional<PluginNode> read_plugin_node(const Value& node);

/// @brief Whether @p node is `null`.
/// @param node A value read from a document.
/// @return True for the literal `null`.
[[nodiscard]] bool is_null(const Value& node);

} // namespace deluge::serialization::schema
