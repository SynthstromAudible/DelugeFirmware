#pragma once

#include "project/owner_plugin.h"
#include "project/param_address.h"
#include "project/song.h"
#include "ui/controls/board.h"
#include "ui/scenes/bind/knob_focus.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace deluge::ui::scenes::bind {

/// @brief A gold knob that edits one of the song's compressor settings, which has no param: its setting's path. Only
///        the song's knobs resolve to one; the knobs write it to the song's chain.
struct CompArm {
	std::string_view path;
	bool operator==(const CompArm&) const = default;
};

/// @brief What a gold knob moves: a param by address, a compressor setting, or nothing.
struct KnobAssignment {
	std::variant<std::monostate, project::ParamAddress, CompArm> target;
	/// Whose table the address's ENGINE id is in: the focused owner's plugin.
	project::OwnerPlugin plugin{project::OwnerPlugin::INSTRUMENT};
};

/// @brief What knob @p knob (0 the lower, 1 the upper) of @p focus moves, from the song's records: the mode, a sound's
///        controls, a chain's view, a MIDI track's CCs, and the focus's compressor view.
/// @param board The board the knobs are on: a knob past its slots moves nothing.
[[nodiscard]] KnobAssignment knob_assignment(const project::Song& song, const KnobFocus& focus, int32_t knob,
                                             const ui::controls::Board& board);

} // namespace deluge::ui::scenes::bind
