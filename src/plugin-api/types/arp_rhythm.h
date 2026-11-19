#pragma once

/// @file
/// @brief The arpeggiator's preset rhythms: which of a pattern's steps play a note and which rest. The rhythm param's
///        menu value indexes the table, so it holds exactly one entry per menu step.

#include "plugin-api/types/display_mapping.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace deluge::plugin {

/// @brief One preset rhythm.
struct ArpRhythm {
	std::string_view name;       ///< as the menus show it: '0' a note, '-' a rest
	uint8_t length{1};           ///< steps in the pattern, 1 to 6
	std::array<bool, 6> steps{}; ///< whether each step plays; only the first @ref length count
};

/// @brief Every preset rhythm, indexed by the rhythm param's menu value.
inline constexpr std::array<ArpRhythm, kMaxMenuSteps + 1> kArpRhythms{{
    {.name = "None", .length = 1, .steps = {true, true, true, true, true, true}},
    {.name = "0--", .length = 3, .steps = {true, false, false, true, true, true}},
    {.name = "00-", .length = 3, .steps = {true, true, false, true, true, true}},
    {.name = "0-0", .length = 3, .steps = {true, false, true, true, true, true}},
    {.name = "0-00", .length = 4, .steps = {true, false, true, true, true, true}},
    {.name = "00--", .length = 4, .steps = {true, true, false, false, true, true}},
    {.name = "000-", .length = 4, .steps = {true, true, true, false, true, true}},
    {.name = "0--0", .length = 4, .steps = {true, false, false, true, true, true}},
    {.name = "00-0", .length = 4, .steps = {true, true, false, true, true, true}},
    {.name = "0----", .length = 5, .steps = {true, false, false, false, false, true}},
    {.name = "0-000", .length = 5, .steps = {true, false, true, true, true, true}},
    {.name = "00---", .length = 5, .steps = {true, true, false, false, false, true}},
    {.name = "0000-", .length = 5, .steps = {true, true, true, true, false, true}},
    {.name = "0---0", .length = 5, .steps = {true, false, false, false, true, true}},
    {.name = "00-00", .length = 5, .steps = {true, true, false, true, true, true}},
    {.name = "0-0--", .length = 5, .steps = {true, false, true, false, false, true}},
    {.name = "000-0", .length = 5, .steps = {true, true, true, false, true, true}},
    {.name = "0--0-", .length = 5, .steps = {true, false, false, true, false, true}},
    {.name = "0--00", .length = 5, .steps = {true, false, false, true, true, true}},
    {.name = "000--", .length = 5, .steps = {true, true, true, false, false, true}},
    {.name = "00--0", .length = 5, .steps = {true, true, false, false, true, true}},
    {.name = "0-00-", .length = 5, .steps = {true, false, true, true, false, true}},
    {.name = "00-0-", .length = 5, .steps = {true, true, false, true, false, true}},
    {.name = "0-0-0", .length = 5, .steps = {true, false, true, false, true, true}},
    {.name = "0-----", .length = 6, .steps = {true, false, false, false, false, false}},
    {.name = "0-0000", .length = 6, .steps = {true, false, true, true, true, true}},
    {.name = "00----", .length = 6, .steps = {true, true, false, false, false, false}},
    {.name = "00000-", .length = 6, .steps = {true, true, true, true, true, false}},
    {.name = "0----0", .length = 6, .steps = {true, false, false, false, false, true}},
    {.name = "00-000", .length = 6, .steps = {true, true, false, true, true, true}},
    {.name = "0-0---", .length = 6, .steps = {true, false, true, false, false, false}},
    {.name = "0000-0", .length = 6, .steps = {true, true, true, true, false, true}},
    {.name = "0---0-", .length = 6, .steps = {true, false, false, false, true, false}},
    {.name = "000-00", .length = 6, .steps = {true, true, true, false, true, true}},
    {.name = "0--000", .length = 6, .steps = {true, false, false, true, true, true}},
    {.name = "000---", .length = 6, .steps = {true, true, true, false, false, false}},
    {.name = "0000--", .length = 6, .steps = {true, true, true, true, false, false}},
    {.name = "0---00", .length = 6, .steps = {true, false, false, false, true, true}},
    {.name = "00--00", .length = 6, .steps = {true, true, false, false, true, true}},
    {.name = "0-00--", .length = 6, .steps = {true, false, true, true, false, false}},
    {.name = "000--0", .length = 6, .steps = {true, true, true, false, false, true}},
    {.name = "0--00-", .length = 6, .steps = {true, false, false, true, true, false}},
    {.name = "0-0-00", .length = 6, .steps = {true, false, true, false, true, true}},
    {.name = "00-0--", .length = 6, .steps = {true, true, false, true, false, false}},
    {.name = "000-0-", .length = 6, .steps = {true, true, true, false, true, false}},
    {.name = "0--0-0", .length = 6, .steps = {true, false, false, true, false, true}},
    {.name = "0-000-", .length = 6, .steps = {true, false, true, true, true, false}},
    {.name = "00---0", .length = 6, .steps = {true, true, false, false, false, true}},
    {.name = "00--0-", .length = 6, .steps = {true, true, false, false, true, false}},
    {.name = "0-0--0", .length = 6, .steps = {true, false, true, false, false, true}},
    {.name = "00-0-0", .length = 6, .steps = {true, true, false, true, false, true}},
}};

/// @brief Whether step @p step of rhythm @p rhythm plays a note; the step wraps at the pattern's length.
/// @param rhythm The rhythm param's menu value, 0 to kMaxMenuSteps.
/// @param step Any step count.
[[nodiscard]] constexpr bool arp_rhythm_plays(uint8_t rhythm, uint32_t step) {
	const ArpRhythm& r = kArpRhythms[rhythm];
	return r.steps[step % r.length];
}

} // namespace deluge::plugin
