#pragma once

/// @brief `instrumentClipView`'s note and note-row state, reached without an include (spec §5.4).
///
/// The note editor and note-row editor (Task 5's 25 leaves) read and write sixteen
/// `instrumentClipView` members named across `gui/menu_item/note/` and `note_row/`, plus the
/// iterance preset labels (`util/lookuptables/lookuptables.cpp:508`) -- all of it behind
/// `gui/views/instrument_clip_view.h`, which `src/scenes` may not include. `NoteBridge` is a
/// struct of function pointers instead: declared here with no legacy include, installed once at
/// boot from `gui/ui/shell_note_bridge.cpp`, and called synchronously -- legacy calls
/// `readValueAgain()` on the line after each write, so every `adjust_*`/`set_*` entry here does the
/// write and the caller re-reads, exactly as legacy's `selectEncoderAction` overrides do.
///
/// **Reads that allocate.** The note-row entries create a `NoteRow` for a non-kit clip when none
/// exists yet, the way `SelectedNoteRow::getIndividualNoteRow` does
/// (`gui/menu_item/note_row/selected_note_row.h:45-61`); the bridge keeps that.
///
/// **Iterance crosses as a raw `int32_t`** (`Iterance::toInt`/`fromInt`,
/// `model/iterance/iterance.h`, which `src/scenes` may include) -- not as a preset index. Task 5's
/// leaves convert with `Iterance::fromInt(...).toPresetIndex()` themselves.
///
/// **A read with nothing selected returns 0**, matching legacy's own "no pressed note" case; the
/// popup and edit guard belong to Task 5's `Binding::permitted` hook, not to this struct.

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::scenes::menu {

/// @brief Function pointers onto `instrumentClipView`'s note and note-row state. Every entry is a
///        transcription of the legacy statement named in its comment; see `shell_note_bridge.cpp`
///        for the citations.
struct NoteBridge {
	// Note editor -- `gui/menu_item/note/*.h`.
	bool (*note_selected)();                         // `SelectedNote::shouldEnterSubmenu`'s test, no popup
	int32_t (*note_velocity)();                      // `gridSquareInfo[y][x].averageVelocity`
	void (*adjust_note_velocity)(int32_t offset);    // `adjustVelocity`
	int32_t (*note_probability)();                   // leftmost pressed note's `getProbability()`
	void (*adjust_note_probability)(int32_t offset); // `adjustNoteProbabilityWithOffset`
	int32_t (*note_iterance)();                      // leftmost pressed note's `getIterance().toInt()`
	void (*adjust_note_iterance)(int32_t offset);    // `adjustNoteIteranceWithOffset`
	void (*set_note_iterance)(int32_t iterance);     // `adjustNoteIteranceWithFinalValue(Iterance::fromInt(...))`
	int32_t (*note_fill)();                          // leftmost pressed note's `getFill()`
	void (*adjust_note_fill)(int32_t offset);        // `adjustNoteFillWithOffset`

	// Note-row editor -- `gui/menu_item/note_row/*.h`. Reads create the row on a non-kit clip.
	bool (*row_auditioning)();                      // `isUIModeActive(UI_MODE_AUDITIONING)`
	int32_t (*row_probability)();                   // `noteRow->probabilityValue`
	void (*adjust_row_probability)(int32_t offset); // `setNoteRowProbabilityWithOffset`
	int32_t (*row_iterance)();                      // `noteRow->iteranceValue.toInt()`
	void (*adjust_row_iterance)(int32_t offset);    // `setNoteRowIteranceWithOffset`
	void (*set_row_iterance)(int32_t iterance);     // `setNoteRowIteranceWithFinalValue`
	int32_t (*row_fill)();                          // `noteRow->fillValue`
	void (*adjust_row_fill)(int32_t offset);        // `setNoteRowFillWithOffset`, plus its fill-mode repaint
	bool (*row_sequence_direction)(int32_t* out);   // false when there is no row
	void (*set_row_sequence_direction)(int32_t value);

	// Shared.
	size_t (*iterance_preset_label)(int32_t preset, bool compact, std::span<char> out); // `iterancePresets`
};

/// @brief Install the bridge the application wires at boot, or `nullptr` to clear it (a failing
///        spec example must be able to take back down whatever it installed).
void set_note_bridge(const NoteBridge* bridge);

/// @brief The installed bridge, or `nullptr` until `set_note_bridge` has been called.
const NoteBridge* note_bridge();

} // namespace deluge::scenes::menu
