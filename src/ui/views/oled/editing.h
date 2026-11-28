#pragma once
#include "ui/widgets/core/peaks.h"
#include "ui/widgets/oled/columns.h"
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/list.h"
#include "ui/widgets/oled/timeline_strips.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The editing moments: zoom, quantize, a held step, the automation lane, the slicer, moving a kit row,
///        randomized drums and undo.

namespace deluge::ui::views::oled {

/// @brief Holding to zoom horizontally.
struct ZoomHeld {
	std::string_view title;  ///< "ZOOM".
	std::string_view length; ///< "4 BARS": the title's note, the clip's length.
	std::string_view zoom;   ///< "1/16": the zoom, big.
	int32_t total;           ///< The clip, in any unit ...
	int32_t start;           ///< ... and the view's window on it.
	int32_t len;
};

/// @brief `title_note(title, length)`, the zoom big, and `position_bar` on the last 7 rows.
void zoom_held(widgets::oled::OledFrame& f, const ZoomHeld& d);

/// @brief Holding quantize.
struct QuantizeHeld {
	std::string_view title;         ///< "QUANTIZE".
	std::string_view amount_text;   ///< "60%": the title's note.
	int32_t length_ticks;           ///< The clip's length.
	int32_t grid_ticks;             ///< The grid quantizing snaps to.
	std::span<const int32_t> notes; ///< Each note's start, in ticks.
	float amount;                   ///< 0..1 (clamped).
};

/// @brief `title_note(title, amount_text)` over `quantize_graph` in the body.
void quantize_held(widgets::oled::OledFrame& f, const QuantizeHeld& d);

/// @brief Holding a step: its parameters as columns.
struct StepHeld {
	std::string_view title;                         ///< "C3 STEP 5".
	std::span<const widgets::oled::Column> columns; ///< The step's parameters, each with its render.
};

/// @brief `columns` over the frame with the title, unfocused and reading no input.
void step_held(widgets::oled::OledFrame& f, const StepHeld& d);

/// @brief The automation moment: a parameter's whole lane.
struct AutomationMoment {
	std::string_view param; ///< "LPF FREQ".
	std::string_view note;  ///< "AUTO": the title's note.
	int32_t length_ticks;   ///< The clip's length.
	std::span<const widgets::oled::LaneNode> nodes;
	int32_t empty_value;  ///< The parameter's value when the lane has no nodes, 0..127.
	int32_t window_start; ///< The pads' window, in ticks ...
	int32_t window_len;   ///< ... (0: none).
	int32_t playhead;     ///< In ticks; < 0: none.
};

/// @brief `title_note(param, note)` over `automation_lane` in the body.
void automation_moment(widgets::oled::OledFrame& f, const AutomationMoment& d);

/// @brief The slicer.
struct Slicer {
	std::string_view title;                        ///< "SLICE 5": the focused slice.
	std::string_view count;                        ///< "16": the title's note.
	std::string_view start;                        ///< "0.460S": last row, left.
	std::string_view length;                       ///< "0.115S": last row, right.
	std::span<const widgets::core::PeakCol> peaks; ///< One per column of the waveform.
	std::span<const int32_t> slice_cols;           ///< Each slice's column in the waveform.
	int32_t focused;                               ///< The focused slice's index in @p slice_cols; -1: none.
};

/// @brief `title_note(title, count)`, `sliced_waveform` from the first text row's top to the row above the last text
///        row, and the last row (start, length).
void slicer(widgets::oled::OledFrame& f, const Slicer& d);

/// @brief A moment that is a title over a list of rows.
struct ListMoment {
	std::string_view title; ///< "MOVE ROW".
	std::string_view note;  ///< "3>5": the title's note; empty for none.
	std::span<const widgets::oled::ListRow> rows;
	int16_t current; ///< The selected row.
};

/// @brief Moving a kit row: `title_note(title, note)` over the rows around the moved one, selected.
void moving_kit_row(widgets::oled::OledFrame& f, const ListMoment& d);

/// @brief Randomized drums: `title_note(title, note)` over each row and the sample it got, the current one selected.
void randomized_drums(widgets::oled::OledFrame& f, const ListMoment& d);

/// @brief Undo or redo: what was undone, and on what.
struct Undo {
	std::string_view title;      ///< "UNDO".
	std::string_view left;       ///< "3 LEFT": the title's note.
	std::string_view what;       ///< "MOVED 4 NOTES": first row.
	std::string_view where;      ///< "LEAD 2": second row.
	std::string_view redo_label; ///< "REDO": third row, left.
	std::string_view redo_hint;  ///< "SHIFT+UNDO": third row, right.
};

/// @brief `title_note(title, left)` over three rows: what, where, and how to redo.
void undo(widgets::oled::OledFrame& f, const Undo& d);

} // namespace deluge::ui::views::oled
