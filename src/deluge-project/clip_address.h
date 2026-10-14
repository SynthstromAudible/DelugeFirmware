#pragma once

#include <compare>
#include <cstdint>

namespace deluge::project {

/// @brief The durable structural id of a clip (the clip owner tier for structural Edits: set-length, clear, delete,
/// multiply, shift, paste).
///
/// Parallel to NoteRowAddress's embedded clip identity and to ParamAddress.
struct ClipAddress {
	uint32_t clip_id{0}; ///< Clip::id() — always set (minted at construction).

	auto operator<=>(const ClipAddress&) const = default;
};

} // namespace deluge::project
