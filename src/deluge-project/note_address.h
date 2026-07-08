#pragma once

#include <cstdint>

namespace deluge::project {

/// The durable structural id of a MELODIC note-row (parallel to ParamAddress). Kit rows (a drum-id row-key)
/// are added in M2c.
struct NoteRowAddress {
	uint32_t clip_id{0}; ///< Clip::serialId() when saved, else sessionId().
	bool clip_is_session{false};
	int32_t y{0}; ///< noteRow->y — the stable melodic row id.

	auto operator<=>(const NoteRowAddress&) const = default;
};

} // namespace deluge::project
