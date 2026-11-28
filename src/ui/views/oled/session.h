#pragma once
#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/timeline_strips.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The session's screens: resting, a section queued, and the moments of holding a clip or a section pad.
///
/// Every word arrives finished (formatted and localised by the scene); a view formats nothing.

namespace deluge::ui::views::oled {

/// @brief The session at rest.
struct SessionResting {
	std::string_view position; ///< "17.3.1": bar.beat.tick, top left.
	std::string_view tempo;    ///< "120.0": top right.
	std::string_view song;     ///< The song's name, centred in the title face.
	std::string_view scale;    ///< "C MINOR": third row, left.
	std::string_view swing;    ///< "SW 56%": third row, right.
	bool metronome;            ///< Draws the metronome icon left of the tempo.
	float level_l;             ///< The master level, 0..1 (clamped), L ...
	float level_r;             ///< ... and R, as two lines along the bottom.
};

/// @brief The session at rest: position and tempo on the top row (the metronome icon 10 px left of the tempo when
///        on), the song's name centred between that row and the third, which is 13 rows above the bottom; the master
///        level as two `level_lines` on the last two rows.
void session_resting(widgets::oled::OledFrame& f, const SessionResting& d);

/// @brief The session with a section or clip queued to launch.
struct SessionQueued {
	std::string_view position;   ///< As `SessionResting::position`.
	std::string_view tempo;      ///< As `SessionResting::tempo`.
	std::string_view next_name;  ///< What is queued: "CHORUS".
	std::string_view next_label; ///< "NEXT": third row, left.
	std::string_view when;       ///< "IN 2 BARS": third row, right.
	bool metronome;              ///< As `SessionResting::metronome`.
	float progress;              ///< 0..1 (clamped) through the count to the launch.
};

/// @brief `session_resting`'s layout with the queued name, "NEXT" and when, and a pair of `amount_line`s filling
///        through the count on the last two rows.
void session_queued(widgets::oled::OledFrame& f, const SessionQueued& d);

/// @brief Holding a clip's pad in the session.
struct ClipHeld {
	std::string_view name;   ///< The clip's output: "LEAD 2".
	std::string_view type;   ///< "SYNTH": the title's note.
	std::string_view length; ///< "4 BARS": first row, left.
	std::string_view launch; ///< "LAUNCH 1 BAR": first row, right.
	int32_t length_ticks;    ///< The clip's length, for the strip.
	std::span<const widgets::oled::StripNote> notes;
	uint8_t low;  ///< The strip's lowest pitch ...
	uint8_t high; ///< ... and highest.
};

/// @brief `title_note(name, type)`, one text row (length, launch), and the clip's notes in `note_strip` across the
///        body under it, from three rows below `body_rect(f, 1)`'s top.
void clip_held(widgets::oled::OledFrame& f, const ClipHeld& d);

/// @brief Holding a section pad.
struct SectionHeld {
	std::string_view name;                 ///< "SECTION B".
	std::string_view position;             ///< "2/6": the title's note.
	std::span<const detail::RowText> rows; ///< REPEATS 4, CLIPS 3, PLAYED 1 OF 4.
};

/// @brief `title_note(name, position)` over `text_rows(rows)`.
void section_held(widgets::oled::OledFrame& f, const SectionHeld& d);

} // namespace deluge::ui::views::oled
