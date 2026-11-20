#pragma once

/// @file
/// @brief A sound's routing in its element: the `sideChainSend` attribute and the `midiOutput` element. The
/// instrument's codec places them among its own entries (deluge::instrument::encode_pieces); these read and write them.

#include "deluge-project/instrument_slot.h"
#include "serialization/value.h"

#include <array>
#include <string_view>

namespace deluge::project {

/// @brief The keys a sound element holds a routing under, in file order.
inline constexpr std::array<std::string_view, 2> kSoundRoutingKeys{"sideChainSend", "midiOutput"};

/// @brief @p routing's entries as a sound element holds them: the send (none at 0) and the MIDI output, its channel
/// and note 255 for none.
/// @param routing The routing.
/// @return A Map of the entries, keyed as kSoundRoutingKeys.
[[nodiscard]] serialization::Value encode_routing(const SoundRouting& routing);

/// @brief The routing @p element (a sound element) holds; a routing with no send and no MIDI output where it holds
/// neither entry.
/// @param element The sound element.
/// @return The routing.
[[nodiscard]] SoundRouting decode_routing(const serialization::Value& element);

} // namespace deluge::project
