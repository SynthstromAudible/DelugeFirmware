#pragma once
#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The recording moments: arming, the count-in, overdubbing, resampling and exporting stems.

namespace deluge::ui::views::oled {

/// @brief Viewing record arming: what will record, from where.
struct RecordArming {
	std::string_view title;                   ///< "ARMED".
	std::string_view count;                   ///< "3": the title's note.
	std::span<const detail::RowText> sources; ///< Each armed output and its source: "LEAD 2" / "MIDI".
};

/// @brief `title_note(title, count)` over a text row per source.
void record_arming(widgets::oled::OledFrame& f, const RecordArming& d);

/// @brief The count-in before recording.
struct CountIn {
	std::string_view title; ///< "COUNT-IN".
	std::string_view note;  ///< "REC": the title's note.
	std::string_view beat;  ///< "3": the beat counting down, big.
	int32_t beats;          ///< The bar's beats ...
	int32_t beats_done;     ///< ... and how many have passed (clamped to 0..beats).
};

/// @brief `title_note(title, note)`, the beat big, and the bar's `beat_boxes` (8 wide, 5 apart) on the last 5 rows.
void count_in(widgets::oled::OledFrame& f, const CountIn& d);

/// @brief Recording a loop into a clip.
struct Overdub {
	std::string_view title;    ///< "OVERDUB".
	std::string_view pass;     ///< "PASS 3": the title's note.
	std::string_view position; ///< "2.3": where in the loop, big.
	float progress;            ///< 0..1 (clamped) through the loop.
};

/// @brief `title_note(title, pass)`, the position big, and a pair of `amount_line`s on the last 2 rows.
void overdub(widgets::oled::OledFrame& f, const Overdub& d);

/// @brief Resampling the master.
struct Resampling {
	std::string_view title;      ///< "RESAMPLING".
	std::string_view elapsed;    ///< "0:12.4": MetricBold13, left.
	std::string_view stop_label; ///< "LOOP END": when it stops, right.
	float level_l;               ///< The level going in, 0..1 (clamped), L ...
	float level_r;               ///< ... and R.
};

/// @brief The title with a 5x5 record mark at the title band's right (the title stops 4 px short of it), the elapsed
///        time in MetricBold13 on the left, the stop label on the right, and `level_lines` on the last 2 rows.
void resampling(widgets::oled::OledFrame& f, const Resampling& d);

/// @brief Exporting stems.
struct StemExport {
	std::string_view title;      ///< "EXPORT STEMS".
	std::string_view of;         ///< "3/8": the title's note.
	std::string_view stem;       ///< "BASS": first row, left.
	std::string_view time;       ///< "0:42": first row, right.
	std::string_view next_label; ///< "NEXT": last row, right.
	std::string_view next;       ///< "MIXDOWN": last row, left.
	int32_t done;                ///< The stem's progress: done of total (clamped; total <= 0 draws the outline).
	int32_t total;
};

/// @brief `title_note(title, of)`, a text row (stem, time), a `progress` bar 7 rows tall just above the last row,
///        and the last row (next, next_label).
void stem_export(widgets::oled::OledFrame& f, const StemExport& d);

} // namespace deluge::ui::views::oled
