/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#pragma once
#include "gui/l10n/l10n.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::osc {
class AudioRecorder final : public MenuItem {
public:
	using MenuItem::MenuItem;
	void beginSession(MenuItem* navigatedBackwardFrom) override {
		soundEditor.shouldGoUpOneLevelOnBegin = true;
		bool success = openUI(&audioRecorder);
		if (!success) {
			if (getCurrentUI() == &soundEditor) {
				soundEditor.goUpOneLevel();
			}
			uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
		}
		else {
			audioRecorder.process();
		}
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SynthMode::SUBTRACTIVE);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {

		bool can = isRelevant(modControllable, whichThing);
		if (!can) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_RECORD_AUDIO_FM_MODE));
			return MenuPermission::NO;
		}

		Sound* sound = static_cast<Sound*>(modControllable);

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, currentRange);
	}
	bool shortcutToHorizontalMenuAllowed() const override { return false; }
};
} // namespace deluge::gui::menu_item::osc
