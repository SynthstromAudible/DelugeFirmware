#pragma once

#include "deluge-project/param_address.h"
#include "engine-api/types/param.h"

#include <cstdint>

namespace deluge::scenes::bind {

/// @brief How far a gold knob turns: a cable's depth reaches further down, a MIDI CC stops short of the top.
enum class KnobKind : uint8_t { PARAM, CABLE, MIDI_CC };

/// @brief The kind of knob @p address is turned by.
[[nodiscard]] KnobKind knob_kind(const project::ParamAddress& address);

/// @brief The gold knob's position (-64..64) for @p value of the param at @p address.
/// @param address The param.
/// @param info    Its descriptor row; null for a cable, an expression dimension or a MIDI CC.
/// @param value   The stored word.
/// @return The knob position.
[[nodiscard]] int32_t knob_pos_of(const project::ParamAddress& address, const engine::ParamInfo* info, int32_t value);

/// @brief The stored word a gold knob at @p knob_pos sets the param at @p address to.
/// @param address  The param.
/// @param info     Its descriptor row; null for a cable, an expression dimension or a MIDI CC.
/// @param knob_pos The knob position.
/// @return The stored word.
[[nodiscard]] int32_t value_of_knob(const project::ParamAddress& address, const engine::ParamInfo* info,
                                    int32_t knob_pos);

/// @brief Whether the knob's indicator blinks at the param's middle value.
/// @param address           The param.
/// @param info              Its descriptor row; null for a cable, an expression dimension or a MIDI CC.
/// @param stutter_quantized Whether the owner's stutter (or the song's, when it uses the song's) is quantized.
/// @param stuttering        Whether a stutter is playing.
/// @return True for a cable, a row marked indicates_middle, and a stutter rate unless quantized and not playing.
[[nodiscard]] bool indicates_middle(const project::ParamAddress& address, const engine::ParamInfo* info,
                                    bool stutter_quantized, bool stuttering);

/// @brief Whether the param at @p address is centred on zero: a bipolar row, a cable's depth, or pitch bend.
[[nodiscard]] bool bipolar(const project::ParamAddress& address, const engine::ParamInfo* info);

/// @brief The knob position after a turn of @p offset from @p knob_pos: clamped below at -64 (a cable's at -192),
///        or wherever it already is lower, and above at 64 (a MIDI CC's at 63).
[[nodiscard]] int32_t turn(KnobKind kind, int32_t knob_pos, int32_t offset);

/// @brief A cable's knob position (-192..64) folded onto its indicator, centred on 64 and reaching it only at 0.
[[nodiscard]] int32_t fold_cable(int32_t knob_pos);

} // namespace deluge::scenes::bind
