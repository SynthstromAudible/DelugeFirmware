/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief The note bridge's consumer. See `shell_note_bridge.h` for why it lives here and what
///        stays on the other side of the wall.
///
/// One static function per `NoteBridge` entry, each a transcription of the legacy statement cited
/// on it -- `gui/menu_item/note/*.h` and `gui/menu_item/note_row/*.h`, which `src/scenes` may not
/// include (spec §5.4). Every read that legacy leaves unanswered when nothing is selected (no
/// pressed note, no note row) returns 0 here; the popup and the edit guard belong to Task 5's
/// `Binding::permitted` hook.

#include "gui/ui/shell_note_bridge.h"

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/iterance/iterance.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "util/lookuptables/lookuptables.h"

#include "scenes/menu/note_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace deluge::gui::ui {
namespace {

namespace menu = deluge::scenes::menu;

// -----------------------------------------------------------------------------------------------
// Note editor -- `gui/menu_item/note/*.h`.
// -----------------------------------------------------------------------------------------------

/// @brief `SelectedNote::shouldEnterSubmenu`'s test, without its popup
///        (`gui/menu_item/note/selected_note.h:29-39`). The popup is Task 5's guard.
bool note_selected() {
	int32_t x_display = instrumentClipView.lastSelectedNoteXDisplay;
	int32_t y_display = instrumentClipView.lastSelectedNoteYDisplay;
	if (x_display != kNoSelection && y_display != kNoSelection) {
		if (instrumentClipView.gridSquareInfo[y_display][x_display].isValid) {
			return true;
		}
	}
	return false;
}

/// @brief `gridSquareInfo[y][x].averageVelocity`, under `Velocity::readCurrentValue`'s own
///        validity test (`gui/menu_item/note/velocity.h:44-52`).
int32_t note_velocity() {
	int32_t xDisplay = instrumentClipView.lastSelectedNoteXDisplay;
	int32_t yDisplay = instrumentClipView.lastSelectedNoteYDisplay;
	if (xDisplay != kNoSelection && yDisplay != kNoSelection) {
		if (instrumentClipView.gridSquareInfo[yDisplay][xDisplay].isValid) {
			return instrumentClipView.gridSquareInfo[yDisplay][xDisplay].averageVelocity;
		}
	}
	return 0;
}

/// @brief `Velocity::selectEncoderAction`'s write, `instrumentClipView.adjustVelocity`
///        (`gui/menu_item/note/velocity.h:54-57`).
void adjust_note_velocity(int32_t offset) {
	instrumentClipView.adjustVelocity(offset);
}

/// @brief Leftmost pressed note's `getProbability()` (`gui/menu_item/note/probability.h:44-50`);
///        a null leftmost note returns 0.
int32_t note_probability() {
	Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();
	if (leftMostNote != nullptr) {
		return leftMostNote->getProbability();
	}
	return 0;
}

/// @brief `Probability::selectEncoderAction`'s write, `adjustNoteProbabilityWithOffset`
///        (`gui/menu_item/note/probability.h:52-55`).
void adjust_note_probability(int32_t offset) {
	instrumentClipView.adjustNoteProbabilityWithOffset(offset);
}

/// @brief Leftmost pressed note's `getIterance().toInt()` (`gui/menu_item/note/iterance_preset.h:44-52`
///        reads the same note's iterance, as a raw int rather than a preset index -- Task 5's leaf
///        converts with `Iterance::toPresetIndex()` itself, since `model/iterance/` is includable
///        from `src/scenes`); a null leftmost note returns 0.
int32_t note_iterance() {
	Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();
	if (leftMostNote != nullptr) {
		Iterance iterance = leftMostNote->getIterance();
		return iterance.toInt();
	}
	return 0;
}

/// @brief `IterancePreset::selectEncoderAction`'s write, `adjustNoteIteranceWithOffset`
///        (`gui/menu_item/note/iterance_preset.h:54-57`).
void adjust_note_iterance(int32_t offset) {
	instrumentClipView.adjustNoteIteranceWithOffset(offset);
}

/// @brief `adjustNoteIteranceWithFinalValue(Iterance::fromInt(...))` -- the CUSTOM iterance
///        editor's commit, called by `note::IteranceDivisor::writeCurrentValue`
///        (`gui/menu_item/note/iterance_divisor.h:67`) and
///        `note::IteranceStepToggle::writeCurrentValue`
///        (`gui/menu_item/note/iterance_step_toggle.h:65`).
void set_note_iterance(int32_t iterance) {
	instrumentClipView.adjustNoteIteranceWithFinalValue(Iterance::fromInt(iterance));
}

/// @brief Leftmost pressed note's `getFill()` (`gui/menu_item/note/fill.h:44-50`); a null leftmost
///        note returns 0.
int32_t note_fill() {
	Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();
	if (leftMostNote != nullptr) {
		return leftMostNote->getFill();
	}
	return 0;
}

/// @brief `Fill::selectEncoderAction`'s write, `adjustNoteFillWithOffset`
///        (`gui/menu_item/note/fill.h:52-55`).
void adjust_note_fill(int32_t offset) {
	instrumentClipView.adjustNoteFillWithOffset(offset);
}

// -----------------------------------------------------------------------------------------------
// Note-row editor -- `gui/menu_item/note_row/*.h`. Reads create the row on a non-kit clip.
// -----------------------------------------------------------------------------------------------

/// @brief `SelectedNoteRow::shouldEnterSubmenu`'s test, without its popup
///        (`gui/menu_item/note_row/selected_note_row.h:34-40`).
bool row_auditioning() {
	return isUIModeActive(UI_MODE_AUDITIONING);
}

/// @brief `SelectedNoteRow::getIndividualNoteRow`, transcribed exactly
///        (`gui/menu_item/note_row/selected_note_row.h:45-61`) -- including that it creates the
///        row on a non-kit clip when none exists yet.
ModelStackWithNoteRow* individualNoteRow(ModelStackWithTimelineCounter* modelStack) {
	auto* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
	                                                                        modelStack); // don't create

	bool isKit = clip->output->type == OutputType::KIT;

	if (!isKit) {
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) { // if note row doesn't exist yet, create it
			modelStackWithNoteRow =
			    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
		}
	}

	return modelStackWithNoteRow;
}

/// @brief `Probability::readCurrentValue`'s read, `noteRow->probabilityValue`
///        (`gui/menu_item/note_row/probability.h:43-53`); no row answers 0.
int32_t row_probability() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = individualNoteRow(modelStack);
	if (NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull()) {
		return noteRow->probabilityValue;
	}
	return 0;
}

/// @brief `Probability::selectEncoderAction`'s write, `setNoteRowProbabilityWithOffset`
///        (`gui/menu_item/note_row/probability.h:55-61`).
void adjust_row_probability(int32_t offset) {
	(void)instrumentClipView.setNoteRowProbabilityWithOffset(offset);
}

/// @brief `IterancePreset::readCurrentValue`'s read, `noteRow->iteranceValue.toInt()`, as a raw int
///        rather than a preset index (`gui/menu_item/note_row/iterance_preset.h:48-60`, whose
///        `toPresetIndex()` conversion Task 5's leaf performs itself); no row answers 0.
int32_t row_iterance() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = individualNoteRow(modelStack);
	if (NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull()) {
		Iterance iterance = noteRow->iteranceValue;
		return iterance.toInt();
	}
	return 0;
}

/// @brief `IterancePreset::selectEncoderAction`'s write, `setNoteRowIteranceWithOffset`
///        (`gui/menu_item/note_row/iterance_preset.h:62-70`).
void adjust_row_iterance(int32_t offset) {
	(void)instrumentClipView.setNoteRowIteranceWithOffset(offset);
}

/// @brief `setNoteRowIteranceWithFinalValue` -- the CUSTOM iterance editor's commit, called by
///        `note_row::IteranceDivisor::writeCurrentValue`
///        (`gui/menu_item/note_row/iterance_divisor.h:76`) and
///        `note_row::IteranceStepToggle::writeCurrentValue`
///        (`gui/menu_item/note_row/iterance_step_toggle.h:92`).
void set_row_iterance(int32_t iterance) {
	(void)instrumentClipView.setNoteRowIteranceWithFinalValue(Iterance::fromInt(iterance));
}

/// @brief `Fill::readCurrentValue`'s read, `noteRow->fillValue`
///        (`gui/menu_item/note_row/fill.h:44-54`); no row answers 0.
int32_t row_fill() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = individualNoteRow(modelStack);
	if (NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull()) {
		return noteRow->fillValue;
	}
	return 0;
}

/// @brief `Fill::selectEncoderAction`'s write, `setNoteRowFillWithOffset` plus its fill-mode
///        repaint (`gui/menu_item/note_row/fill.h:56-65`).
void adjust_row_fill(int32_t offset) {
	int32_t newValue = instrumentClipView.setNoteRowFillWithOffset(offset);
	if (newValue != -1) {
		if (currentSong->isFillModeActive()) {
			uiNeedsRendering(&instrumentClipView);
		}
	}
}

/// @brief The non-kit auditioned-row arm of `sequence::Direction::getIndividualNoteRow`
///        (`gui/menu_item/sequence/direction.h:50-60`), with two of its three statements dropped on
///        purpose:
///          * `:50`'s own test, `clip->output->type != OutputType::KIT`, IS transcribed below --
///            without it a kit clip would fall into this arm and read/create whatever DRUM's row
///            happens to be displayed at `lastAuditionedYDisplay`, which is not what legacy's kit
///            arm (`:44-49`, keyed on `kit->selectedDrum`) or its final fallback (`:62`) do. Both of
///            those are `bindings.h`'s `read_sequence_direction`/`write_sequence_direction`'s
///            concern, not this bridge's -- a kit clip here answers "no row" (`fix round 1,
///            important 3`: the first cut of this function omitted `:50`'s test entirely).
///          * `:51`'s outer gate, `soundEditor.selectedNoteRow`, is NOT transcribed: it is latched
///            from `isUIModeActive(UI_MODE_AUDITIONING)` at legacy's session begin (`:118`), which
///            the port never calls, so spec §5.5 gates the equivalent condition at the call site
///            instead (Task 6 calls both these entries only under `Root::NoteRowEditor`).
ModelStackWithNoteRow* noteRowForSequenceDirection(ModelStackWithTimelineCounter* modelStack) {
	auto* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
	if (clip->output->type == OutputType::KIT) {
		return modelStack->addNoteRow(0, nullptr); // no row -- legacy's kit arm is not this bridge's concern
	}
	ModelStackWithNoteRow* modelStackWithNoteRow =
	    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) { // if note row doesn't exist yet, create it
		modelStackWithNoteRow =
		    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
	}
	return modelStackWithNoteRow;
}

/// @brief `Direction::readCurrentValue`'s row arm, `noteRow->sequenceDirectionMode`
///        (`gui/menu_item/sequence/direction.h:65-76`); false when there is still no row.
bool row_sequence_direction(int32_t* out) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = noteRowForSequenceDirection(modelStack);
	NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
	if (noteRow == nullptr) {
		return false;
	}
	*out = static_cast<int32_t>(noteRow->sequenceDirectionMode);
	return true;
}

/// @brief `Direction::writeCurrentValue`'s row arm, `NoteRow::setSequenceDirectionMode`
///        (`gui/menu_item/sequence/direction.h:78-90`).
void set_row_sequence_direction(int32_t value) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = noteRowForSequenceDirection(modelStack);
	if (NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull()) {
		noteRow->setSequenceDirectionMode(modelStackWithNoteRow, static_cast<SequenceDirection>(value));
	}
}

// -----------------------------------------------------------------------------------------------
// Shared.
// -----------------------------------------------------------------------------------------------

/// @brief `getIteranceDisplayValue` (`gui/menu_item/note/iterance_preset.h:87-111`), restricted to
///        presets `1..kNumIterancePresets` -- OFF and CUSTOM are the leaf's own literals, not table
///        lookups, so they are not this function's concern. `compact` chooses `"%d:%d"`
///        (`renderInHorizontalMenu`, `:74-78`) over `"%d of %d"` (`drawPixelsForOled`,
///        `:69-72`; `getNotificationValue`, `:80-82`).
size_t iterance_preset_label(int32_t preset, bool compact, std::span<char> out) {
	if (preset < 1 || preset > kNumIterancePresets) {
		return 0;
	}

	Iterance iterance = iterancePresets[preset - 1];
	int32_t i = iterance.divisor;
	// `iterance.iteranceStep[i]` reads index `divisor` of a `std::bitset<8>` on the first turn --
	// legacy's own out-of-range read (`gui/menu_item/note/iterance_preset.h:100-103`), transcribed
	// exactly rather than fixed (Kate's call).
	for (; i >= 0; i--) {
		// try to find which iteration step index is active
		if (iterance.iteranceStep[i]) {
			break;
		}
	}

	char buffer[20];
	int written =
	    compact
	        ? snprintf(buffer, sizeof(buffer), "%d:%d", static_cast<int>(i + 1), static_cast<int>(iterance.divisor))
	        : snprintf(buffer, sizeof(buffer), "%d of %d", static_cast<int>(i + 1), static_cast<int>(iterance.divisor));
	if (written <= 0) {
		return 0;
	}

	size_t n = std::min(static_cast<size_t>(written), out.size());
	if (n > 0) {
		std::memcpy(out.data(), buffer, n);
	}
	return n;
}

const menu::NoteBridge kNoteBridge{
    .note_selected = &note_selected,
    .note_velocity = &note_velocity,
    .adjust_note_velocity = &adjust_note_velocity,
    .note_probability = &note_probability,
    .adjust_note_probability = &adjust_note_probability,
    .note_iterance = &note_iterance,
    .adjust_note_iterance = &adjust_note_iterance,
    .set_note_iterance = &set_note_iterance,
    .note_fill = &note_fill,
    .adjust_note_fill = &adjust_note_fill,
    .row_auditioning = &row_auditioning,
    .row_probability = &row_probability,
    .adjust_row_probability = &adjust_row_probability,
    .row_iterance = &row_iterance,
    .adjust_row_iterance = &adjust_row_iterance,
    .set_row_iterance = &set_row_iterance,
    .row_fill = &row_fill,
    .adjust_row_fill = &adjust_row_fill,
    .row_sequence_direction = &row_sequence_direction,
    .set_row_sequence_direction = &set_row_sequence_direction,
    .iterance_preset_label = &iterance_preset_label,
};

} // namespace

void wireShellNoteBridge() {
	menu::set_note_bridge(&kNoteBridge);
}

} // namespace deluge::gui::ui
