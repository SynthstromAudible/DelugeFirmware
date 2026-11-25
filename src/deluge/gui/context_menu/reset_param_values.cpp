/*
 * Copyright © 2026 Sean Ditny
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

#include "gui/context_menu/reset_param_values.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence_tempo_change.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include <array>
namespace deluge::gui::context_menu {
PLACE_SDRAM_BSS ResetParamValues reset_param_values{};
char const* ResetParamValues::getTitle() {
	return l10n::get(l10n::String::STRING_FOR_ARE_YOU_SURE_QMARK);
}
std::span<char const*> ResetParamValues::getOptions() {
	if (display->haveOLED()) {
		static std::array options = {l10n::get(l10n::String::STRING_FOR_OK)};
		return options;
	}
	static std::array options = {l10n::get(l10n::String::STRING_FOR_SURE)};
	return options;
}
bool ResetParamValues::acceptCurrentOption() {
	std::array<char, MODEL_STACK_MAX_SIZE> memory_storage{};
	char* memory = memory_storage.data();
	auto* stack = soundEditor.getCurrentModelStack(memory);
	// One undoable step, never merged with neighbouring edits.
	actionLogger.closeAction(ActionType::MISC);
	Action* action =
	    actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED, ActionOrigin::RESET_PARAM_VALUES);
	bool song_params = stack->paramManager == &currentSong->paramManager;
	// Undoing the tempo scalar alone would not restore the clock.
	void* memory_for_tempo = (action != nullptr && song_params)
	                             ? GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceTempoChange))
	                             : nullptr;
	// Refuse a reset which couldn't be undone, e.g. while recording the arrangement.
	if (action == nullptr || (song_params && memory_for_tempo == nullptr)) {
		if (action != nullptr) {
			actionLogger.discardNewestAction();
		}
		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_RECORDING_IN_PROGRESS));
		}
		else {
			display->displayError(Error::INSUFFICIENT_RAM);
		}
		return false;
	}
	auto* tempo = currentSong->paramManager.getUnpatchedParamSet()->getClipValueStorage(
	    deluge::modulation::params::UNPATCHED_TEMPO);
	uint64_t time_before = currentSong->timePerTimerTickBig;
	int64_t tempo_before = tempo->getUnclampedValue();
	// Snapshot every value the reset changes before changing any, so a failure here changes nothing.
	Error error = stack->paramManager->resetValuesToPreset(stack, *action, false);
	if (error != Error::NONE) {
		if (memory_for_tempo != nullptr) {
			delugeDealloc(memory_for_tempo);
		}
		actionLogger.discardNewestAction();
		display->displayError(error);
		return false;
	}
	// Failing to recreate a route or CC leaves the values reset so far undoable.
	error = stack->paramManager->resetValuesToPreset(stack, *action, true);
	if (song_params) {
		// Tap tempo and tempo taken from a clip keep an exact clock which the rounded scalar can't hold, so
		// refresh the clock only when the reset changed the tempo scalar.
		if (tempo->getUnclampedValue() != tempo_before) {
			currentSong->updateBPMFromAutomation(true);
		}
		// Ownership passes to the action.
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
		action->addConsequence(new (memory_for_tempo) ConsequenceTempoChange(
		    time_before, currentSong->timePerTimerTickBig, tempo_before, tempo->getUnclampedValue()));
	}
	actionLogger.closeAction(ActionType::MISC);
	if (error != Error::NONE) {
		display->displayError(error);
	}
	return false;
}
} // namespace deluge::gui::context_menu
