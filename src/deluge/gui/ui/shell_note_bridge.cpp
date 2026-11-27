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
/// on it -- `gui/menu_item/note/*.h` and `gui/menu_item/note_row/*.h`, which `src/ui/scenes` may not
/// include. Every read that legacy leaves unanswered when nothing is selected (no pressed note, no
/// note row) returns 0 here; the popup and the edit guard belong to the leaves' `Binding::permitted`
/// hook.

#include "gui/ui/shell_note_bridge.h"
#include "project/sequence_direction.h"

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "project/iterance.h"
#include "util/lookuptables/lookuptables.h"

#include "ui/scenes/menu/note_bridge.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

using deluge::project::Iterance;

namespace deluge::gui::ui {
namespace {

namespace menu = deluge::ui::scenes::menu;

// -----------------------------------------------------------------------------------------------
// Note editor -- `gui/menu_item/note/*.h`.
// -----------------------------------------------------------------------------------------------

/// @brief `SelectedNote::shouldEnterSubmenu`'s test, without its popup
///        (`origin/main:src/deluge/gui/menu_item/note/selected_note.h`): a held note pad, or a remembered one.
bool note_selected() {
	return instrumentClipView.getLeftMostNotePressed() != nullptr;
}

/// @brief `note::Velocity::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/note/velocity.h`): the leftmost
///        held pad's average velocity, else the leftmost remembered edit target's intended velocity.
int32_t note_velocity() {
	const EditPadPress* selected_press = nullptr;
	for (const auto& press : instrumentClipView.editPadPresses) {
		if (press.isActive && (selected_press == nullptr || press.intendedPos < selected_press->intendedPos)) {
			selected_press = &press;
		}
	}
	if (selected_press != nullptr) {
		const auto& square = instrumentClipView.gridSquareInfo[selected_press->yDisplay][selected_press->xDisplay];
		if (square.isValid) {
			return square.averageVelocity;
		}
	}
	auto edit_targets = instrumentClipView.get_note_edit_targets();
	for (const auto& target : edit_targets) {
		if (target.isActive && (selected_press == nullptr || target.intendedPos < selected_press->intendedPos)) {
			selected_press = &target;
		}
	}
	return selected_press != nullptr ? selected_press->intendedVelocity : 0;
}

/// @brief `Velocity::selectEncoderAction`'s write, `instrumentClipView.adjustVelocity(offset, false)`
///        (`origin/main:src/deluge/gui/menu_item/note/velocity.h`): the menu shows the value itself, so no velocity
///        popup.
void adjust_note_velocity(int32_t offset) {
	instrumentClipView.adjustVelocity(offset, false);
}

/// @brief Leftmost pressed note's `getProbability()` (`Probability::readCurrentValue` in
///        `origin/main:src/deluge/gui/menu_item/note/probability.h`); a null leftmost note returns 0.
int32_t note_probability() {
	Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();
	if (leftMostNote != nullptr) {
		return leftMostNote->getProbability();
	}
	return 0;
}

/// @brief `Probability::selectEncoderAction`'s write, `adjustNoteProbabilityWithOffset`
///        (`origin/main:src/deluge/gui/menu_item/note/probability.h`).
void adjust_note_probability(int32_t offset) {
	instrumentClipView.adjustNoteProbabilityWithOffset(offset);
}

/// @brief Leftmost pressed note's `getIterance().packed()` (`IterancePreset::readCurrentValue` in
///        `origin/main:src/deluge/gui/menu_item/note/iterance_preset.h` reads the same note's iterance, as a raw int
///        rather than a preset index -- the scenes' leaf converts with `deluge::project::Iterance::preset_index()`
///        itself, since `model/iterance/` is includable from `src/ui/scenes`); a null leftmost note returns 0.
int32_t note_iterance() {
	Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();
	if (leftMostNote != nullptr) {
		Iterance iterance = leftMostNote->getIterance();
		return iterance.packed();
	}
	return 0;
}

/// @brief `IterancePreset::selectEncoderAction`'s write, `adjustNoteIteranceWithOffset`
///        (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`).
void adjust_note_iterance(int32_t offset) {
	instrumentClipView.adjustNoteIteranceWithOffset(offset);
}

/// @brief `adjustNoteIteranceWithFinalValue(deluge::project::Iterance::from_packed(...))` -- the CUSTOM iterance
///        editor's commit, called by `note::IteranceDivisor::writeCurrentValue`
///        (`origin/main:src/deluge/gui/menu_item/note/iterance_divisor.h`) and
///        `note::IteranceStepToggle::writeCurrentValue`
///        (`origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`).
void set_note_iterance(int32_t iterance) {
	instrumentClipView.adjustNoteIteranceWithFinalValue(deluge::project::Iterance::from_packed(iterance));
}

/// @brief Leftmost pressed note's `getFill()` (`Fill::readCurrentValue` in
///        `origin/main:src/deluge/gui/menu_item/note/fill.h`); a null leftmost note returns 0.
int32_t note_fill() {
	Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();
	if (leftMostNote != nullptr) {
		return leftMostNote->getFill();
	}
	return 0;
}

/// @brief `Fill::selectEncoderAction`'s write, `adjustNoteFillWithOffset`
///        (`origin/main:src/deluge/gui/menu_item/note/fill.h`).
void adjust_note_fill(int32_t offset) {
	instrumentClipView.adjustNoteFillWithOffset(offset);
}

// -----------------------------------------------------------------------------------------------
// Note-row editor -- `gui/menu_item/note_row/*.h`. Reads create the row on a non-kit clip.
// -----------------------------------------------------------------------------------------------

/// @brief `SelectedNoteRow::shouldEnterSubmenu`'s test, without its popup
///        (`origin/main:src/deluge/gui/menu_item/note_row/selected_note_row.h`).
bool row_auditioning() {
	return isUIModeActive(UI_MODE_AUDITIONING);
}

/// @brief `SelectedNoteRow::getIndividualNoteRow`, transcribed exactly
///        (`origin/main:src/deluge/gui/menu_item/note_row/selected_note_row.h`) -- including that it creates the
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
///        (`origin/main:src/deluge/gui/menu_item/note_row/probability.h`); no row answers 0.
int32_t row_probability() {
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = individualNoteRow(modelStack);
	if (NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull()) {
		return noteRow->probabilityValue;
	}
	return 0;
}

/// @brief `Probability::selectEncoderAction`'s write, `setNoteRowProbabilityWithOffset`
///        (`origin/main:src/deluge/gui/menu_item/note_row/probability.h`).
void adjust_row_probability(int32_t offset) {
	(void)instrumentClipView.setNoteRowProbabilityWithOffset(offset);
}

/// @brief `IterancePreset::readCurrentValue`'s read, `noteRow->iteranceValue.packed()`, as a raw int
///        rather than a preset index (`origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`, whose
///        `preset_index()` conversion the scenes' leaf performs itself); no row answers 0.
int32_t row_iterance() {
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = individualNoteRow(modelStack);
	if (NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull()) {
		Iterance iterance = noteRow->iteranceValue;
		return iterance.packed();
	}
	return 0;
}

/// @brief `IterancePreset::selectEncoderAction`'s write, `setNoteRowIteranceWithOffset`
///        (`origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`).
void adjust_row_iterance(int32_t offset) {
	(void)instrumentClipView.setNoteRowIteranceWithOffset(offset);
}

/// @brief `setNoteRowIteranceWithFinalValue` -- the CUSTOM iterance editor's commit, called by
///        `note_row::IteranceDivisor::writeCurrentValue`
///        (`origin/main:src/deluge/gui/menu_item/note_row/iterance_divisor.h`) and
///        `note_row::IteranceStepToggle::writeCurrentValue`
///        (`origin/main:src/deluge/gui/menu_item/note_row/iterance_step_toggle.h`).
void set_row_iterance(int32_t iterance) {
	(void)instrumentClipView.setNoteRowIteranceWithFinalValue(deluge::project::Iterance::from_packed(iterance));
}

/// @brief `Fill::readCurrentValue`'s read, `noteRow->fillValue`
///        (`origin/main:src/deluge/gui/menu_item/note_row/fill.h`); no row answers 0.
int32_t row_fill() {
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = individualNoteRow(modelStack);
	if (NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull()) {
		return noteRow->fillValue;
	}
	return 0;
}

/// @brief `Fill::selectEncoderAction`'s write, `setNoteRowFillWithOffset` plus its fill-mode
///        repaint (`origin/main:src/deluge/gui/menu_item/note_row/fill.h`).
void adjust_row_fill(int32_t offset) {
	int32_t newValue = instrumentClipView.setNoteRowFillWithOffset(offset);
	if (newValue != -1) {
		if (currentSong->isFillModeActive()) {
			uiNeedsRendering(&instrumentClipView);
		}
	}
}

/// @brief The non-kit auditioned-row arm of `sequence::Direction::getIndividualNoteRow`
///        (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`), with two of its three statements dropped on
///        purpose:
///          * Its `else if` test, `clip->output->type != OutputType::KIT`, IS transcribed below --
///            without it a kit clip would fall into this arm and read/create whatever DRUM's row
///            happens to be displayed at `lastAuditionedYDisplay`, which is not what legacy's kit
///            arm (keyed on `kit->selectedDrum`) or its final fallback (`return modelStack->addNoteRow(0, nullptr)`)
///            do. Both of those are `bindings.h`'s `read_sequence_direction`/`write_sequence_direction`'s concern, not
///            this bridge's -- a kit clip here answers "no row".
///          * The outer gate of main's `Direction::getIndividualNoteRow`, `soundEditor.selectedNoteRow`, is NOT
///            transcribed: it is latched from `isUIModeActive(UI_MODE_AUDITIONING)` at the session begin
///            (main's `Direction::checkPermissionToBeginSession`), which
///            the port never calls, so the equivalent condition is gated at the call site
///            instead (both these entries are called only under `Root::NoteRowEditor`).
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
///        (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`); false when there is still no row.
bool row_sequence_direction(int32_t* out) {
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = noteRowForSequenceDirection(modelStack);
	NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
	if (noteRow == nullptr) {
		return false;
	}
	*out = static_cast<int32_t>(noteRow->sequenceDirectionMode());
	return true;
}

/// @brief `Direction::writeCurrentValue`'s row arm, `NoteRow::setSequenceDirectionMode`
///        (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`).
void set_row_sequence_direction(int32_t value) {
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStackWithNoteRow = noteRowForSequenceDirection(modelStack);
	if (NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull()) {
		deluge::edits::noteRowSettingFor(
		    *noteRow, *static_cast<InstrumentClip*>(modelStackWithNoteRow->getTimelineCounter()), "sequenceDirection",
		    deluge::serialization::Value::makeScalar(
		        std::string{deluge::project::to_string(static_cast<SequenceDirection>(value))}));
	}
}

// -----------------------------------------------------------------------------------------------
// Shared.
// -----------------------------------------------------------------------------------------------

/// @brief The iterance preset leaf's label (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`), restricted
///        to presets `1..deluge::project::kIterancePresetCount` -- OFF and CUSTOM are the leaf's own literals, not
///        table lookups, so they are not this function's concern. `compact` chooses the horizontal menu's short
///        `1:2` form over the full-screen and notification `1 of 2` form.
size_t iterance_preset_label(int32_t preset, bool compact, std::span<char> out) {
	if (preset < 1 || preset > deluge::project::kIterancePresetCount) {
		return 0;
	}

	std::array<char, 20> buffer{};
	const size_t length = deluge::project::format_preset(preset, buffer, "", compact ? ":" : " of ",
	                                                     compact ? deluge::project::Iterance::Label::SHORT
	                                                             : deluge::project::Iterance::Label::LONG);

	const size_t n = std::min(length, out.size());
	if (n > 0) {
		std::memcpy(out.data(), buffer.data(), n);
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
