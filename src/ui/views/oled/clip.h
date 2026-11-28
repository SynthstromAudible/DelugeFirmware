#pragma once
#include "ui/widgets/core/peaks.h"
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/list.h"
#include "ui/widgets/oled/misc.h"
#include "ui/widgets/oled/timeline_strips.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The clip views' resting screens: an instrument clip, an audio clip, a kit clip and a CV clip.

namespace deluge::ui::views::oled {

/// @brief An instrument clip at rest.
struct InstrumentClipResting {
	widgets::oled::OutputNameData name; ///< The output's name, as `output_name` draws it.
	int32_t length_ticks;               ///< The clip's length.
	std::span<const widgets::oled::StripNote> notes;
	uint8_t low;          ///< The strip's lowest pitch ...
	uint8_t high;         ///< ... and highest.
	int32_t window_start; ///< The pads' window, in ticks ...
	int32_t window_len;   ///< ... (0: none).
	int32_t playhead;     ///< In ticks; < 0: none.
};

/// @brief `output_name` over the frame, and the clip's notes in a `note_strip` along the last 10 rows with the pads'
///        window and the playhead.
void instrument_clip_resting(widgets::oled::OledFrame& f, const InstrumentClipResting& d);

/// @brief An audio clip at rest.
struct AudioClipResting {
	std::string_view name;                         ///< The sample's name: the title.
	std::string_view length;                       ///< "1.84S": last row, left.
	std::string_view pitch;                        ///< "C#3": last row, right.
	std::span<const widgets::core::PeakCol> peaks; ///< One per column of the waveform, the whole sample.
	int32_t window_x;                              ///< The pads' window, in columns of the waveform ...
	int32_t window_w;                              ///< ... (0: none).
	int32_t playhead_x;                            ///< In columns; < 0: none.
};

/// @brief The title, the whole sample's `waveform` from the first text row's top to the row above the last text row
///        with `window_marks` over it, and the length and pitch on the last row.
void audio_clip_resting(widgets::oled::OledFrame& f, const AudioClipResting& d);

/// @brief A kit clip at rest.
struct KitClipResting {
	std::string_view kit;                         ///< The kit's name: the title.
	std::span<const widgets::oled::ListRow> rows; ///< The drum rows, each with its note count as the value.
	int16_t current;                              ///< The selected drum row.
};

/// @brief The title over the drum rows in `list`, scrolled to and selecting the current one. Consumes no input.
void kit_clip_resting(widgets::oled::OledFrame& f, const KitClipResting& d);

/// @brief A CV clip at rest.
struct CvClipResting {
	std::string_view title;      ///< "CV 1".
	std::string_view gate_label; ///< "GATE 1": the title's note.
	std::string_view voltage;    ///< "2.00V": big.
	std::string_view note;       ///< "C3": last row, left.
	std::string_view gate_state; ///< "GATE ON": last row, right.
};

/// @brief `title_note(title, gate_label)`, the voltage big, and the note and gate state on the last row.
void cv_clip_resting(widgets::oled::OledFrame& f, const CvClipResting& d);

} // namespace deluge::ui::views::oled
