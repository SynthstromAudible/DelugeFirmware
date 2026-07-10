#pragma once

#include <cstdint>

namespace deluge::project {

/// The durable structural id of a kit row: which kit (Output) + which drum. Parallel to ClipAddress/NoteRowAddress.
struct KitRowAddress {
	uint32_t kit_id{0};  ///< Kit(Output)::id() (durable).
	uint32_t drum_id{0}; ///< Drum::id() (durable). 0 = unassigned (a to-be-created row).

	auto operator<=>(const KitRowAddress&) const = default;
};

} // namespace deluge::project
