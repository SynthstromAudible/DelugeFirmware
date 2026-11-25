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

#pragma once
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"

namespace deluge::gui::menu_item {
class TrackLink final : public Toggle {
public:
	using Toggle::Toggle;
	bool isRelevant(ModControllableAudio* /*modControllable*/, int32_t /*whichThing*/) const override {
		return (getCurrentClip() != nullptr) && (soundEditor.currentParamManager != nullptr)
		       && soundEditor.currentParamManager->containsAnyMainParamCollections();
	}
	void readCurrentValue() override { setValue(getCurrentClip()->output->trackLink); }
	void writeCurrentValue() override {
		actionLogger.closeAction(ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE);
		getCurrentClip()->output->trackLink = getValue();
		view.setKnobIndicatorLevels();
		// Controllers receive the value their next CC will edit.
		view.sendMidiFollowFeedback();
	}
};
} // namespace deluge::gui::menu_item
