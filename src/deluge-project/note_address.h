#pragma once

#include <cstdint>

namespace deluge::project {

/// The durable structural id of a note-row (parallel to ParamAddress): which clip, which row. The row is
/// keyed by pitch (`y`) for melodic clips, or by the drum's id for kit clips.
struct NoteRowAddress {
	uint32_t clip_id{0}; ///< Clip::id() when saved, else sessionId().
	bool clip_is_session{false};
	bool is_kit{false};             ///< true: row keyed by a drum id; false: melodic, keyed by y.
	int32_t row_key{0};             ///< noteRow->y (melodic) or the SOUND drum's id (kit).
	bool row_key_is_session{false}; ///< kit only: whether row_key is a session id (vs a serial id).

	auto operator<=>(const NoteRowAddress&) const = default;
};

} // namespace deluge::project
