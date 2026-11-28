#pragma once
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/timeline_strips.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The arranger's screens: the whole arrangement at rest, and the moment of holding one clip instance.

namespace deluge::ui::views::oled {

/// @brief The arranger at rest.
struct ArrangerResting {
	std::string_view song;     ///< The song's name: the title.
	std::string_view position; ///< "17.3": the title's note.
	int32_t lanes;             ///< How many lanes the overview divides its height into.
	int32_t units;             ///< The arrangement's length, in the spans' units (bars, say).
	std::span<const widgets::oled::LaneSpan> spans;
	int32_t window_start; ///< The pads' window, in units ...
	int32_t window_len;   ///< ... (0: none).
	int32_t playhead;     ///< In units; < 0: none.
};

/// @brief `title_note(song, position)` over the whole arrangement in `overview`, from the first text row's top to two
///        rows above the bottom.
void arranger_resting(widgets::oled::OledFrame& f, const ArrangerResting& d);

/// @brief Holding a clip instance in the arranger.
struct ArrangerClipHeld {
	std::string_view name;                         ///< The clip's output: "LEAD 2".
	std::string_view position;                     ///< "BAR 17": the title's note.
	std::string_view length;                       ///< "8 BARS": first row, left.
	std::string_view repeats;                      ///< "X2": first row, right.
	int32_t units;                                 ///< The arrangement's length, in the spans' units.
	std::span<const widgets::oled::LaneSpan> lane; ///< The held lane's instances (lane 0), the held one selected.
};

/// @brief `title_note(name, position)`, one text row (length, repeats), and the held lane as a one-lane `overview`
///        7 rows tall, centred in the body under the text row: the held instance filled, the others outlined.
void arranger_clip_held(widgets::oled::OledFrame& f, const ArrangerClipHeld& d);

} // namespace deluge::ui::views::oled
