#pragma once
#include "ui/widgets/core/peaks.h"
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/list.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The playing moments: auditioning a kit row, stutter, tap tempo, MIDI learn, holding a mod source, and
///        loading a song.

namespace deluge::ui::views::oled {

/// @brief Auditioning a kit row: its sample.
struct AuditioningRow {
	std::string_view drum;                         ///< "KICK 909": the title.
	std::string_view row;                          ///< "ROW 3": the title's note.
	std::string_view velocity;                     ///< "VEL 100": last row, left.
	std::string_view length;                       ///< "0.42S": last row, right.
	std::span<const widgets::core::PeakCol> peaks; ///< One per column of the waveform.
};

/// @brief `title_note(drum, row)`, the sample's `waveform` from the first text row's top to the row above the last,
///        and the last row (velocity, length).
void auditioning_row(widgets::oled::OledFrame& f, const AuditioningRow& d);

/// @brief Holding stutter.
struct StutterHeld {
	std::string_view title; ///< "STUTTER".
	std::string_view rate;  ///< "1/16": big.
	int32_t repeats;        ///< The repeats in view ...
	int32_t repeats_done;   ///< ... and how many have played (`beat_boxes` clamps it to 0..repeats).
};

/// @brief The title, the rate big, and the repeats as `beat_boxes` 6 wide and 2 apart, 4 rows tall, one row above
///        the bottom.
void stutter_held(widgets::oled::OledFrame& f, const StutterHeld& d);

/// @brief Tapping the tempo.
struct TapTempo {
	std::string_view title; ///< "TAP TEMPO".
	std::string_view tempo; ///< "122.4": big.
	int32_t taps;           ///< The taps a reading takes ...
	int32_t taps_done;      ///< ... and how many have landed (`beat_boxes` clamps it to 0..taps).
};

/// @brief The title, the tempo big, and the taps as `beat_boxes` (8 wide, 5 apart) on the last 5 rows.
void tap_tempo(widgets::oled::OledFrame& f, const TapTempo& d);

/// @brief MIDI learn: what is being learned, what is arriving.
struct MidiLearn {
	std::string_view title;   ///< "LEARN".
	std::string_view target;  ///< "LPF FREQ": first row, left.
	std::string_view value;   ///< "74": first row, right.
	std::string_view cc;      ///< "CC 74": second row, left.
	std::string_view channel; ///< "CH 1": second row, right.
	float level;              ///< The arriving value, 0..1 (clamped).
};

/// @brief The title, two text rows, and the arriving level as a pair of `amount_line`s on the last 2 rows.
void midi_learn(widgets::oled::OledFrame& f, const MidiLearn& d);

/// @brief Holding a mod source: everything it is patched to, and by how much.
struct ModSourceHeld {
	std::string_view title;                       ///< "LFO 1 TO".
	std::span<const widgets::oled::ListRow> rows; ///< Each destination and its amount: "LPF FREQ" / "+32".
};

/// @brief The title over the destinations in `list` from the top, none selected.
void mod_source_held(widgets::oled::OledFrame& f, const ModSourceHeld& d);

/// @brief Loading a song.
struct LoadingSong {
	std::string_view title; ///< "LOADING".
	std::string_view song;  ///< "SONG 08": centred in the title face.
	std::string_view label; ///< "SAMPLES": last row, left.
	std::string_view count; ///< "12/40": last row, right.
	int32_t done;           ///< done of total (clamped; total <= 0 draws the outline).
	int32_t total;
};

/// @brief The title, the song's name centred on the first text row, a `progress` bar 7 rows tall just above the last
///        row, and the last row (label, count).
void loading_song(widgets::oled::OledFrame& f, const LoadingSong& d);

} // namespace deluge::ui::views::oled
