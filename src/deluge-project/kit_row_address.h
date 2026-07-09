#pragma once

#include <cstdint>

namespace deluge::project {

/// The durable structural id of a kit row: which kit (Output) + which drum. Parallel to ClipAddress/NoteRowAddress.
struct KitRowAddress {
	uint32_t kit_id{0}; ///< Kit(Output)::id() when saved, else sessionId().
	bool kit_is_session{false};
	uint32_t drum_id{0}; ///< Drum::id() when saved, else sessionId(). 0 = unassigned (a to-be-created row).
	bool drum_is_session{false};

	auto operator<=>(const KitRowAddress&) const = default;
};

} // namespace deluge::project
