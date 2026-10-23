#pragma once

#include "deluge-project/param_address.h"
#include "deluge-project/song.h"
#include "engine-api/types/param_kind.h"
#include "scenes/bind/knob_focus.h"

#include <cstdint>
#include <string_view>
#include <variant>

namespace deluge::scenes::bind {

/// @brief A gold knob that edits one of the song's compressor settings, which has no param: its setting's path.
struct CompArm {
	std::string_view path;
	bool operator==(const CompArm&) const = default;
};

/// @brief What a gold knob moves: a param by address, a compressor setting, or nothing.
struct KnobAssignment {
	std::variant<std::monostate, project::ParamAddress, CompArm> target;
	/// The param's kind, as the popup and the knob's range read it.
	engine::ParamKind kind{engine::ParamKind::NONE};
};

/// @brief The mod button the gold knobs of @p focus follow: the song's, an output's, a kit row's kit's. Past the last
///        button, the last.
[[nodiscard]] uint8_t knob_mode(const project::Song& song, const KnobFocus& focus);

/// @brief What knob @p knob (0 the lower, 1 the upper) of @p focus moves, from the song's records: the mode, a sound's
///        controls, a chain's view, a MIDI track's CCs, and the focus's compressor view.
[[nodiscard]] KnobAssignment knob_assignment(const project::Song& song, const KnobFocus& focus, int32_t knob);

} // namespace deluge::scenes::bind
