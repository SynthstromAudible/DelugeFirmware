/*
 * Copyright (c) 2014-2025 Synthstrom Audible Limited
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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::stutter {

class StutterDirection final : public Selection {
public:
	using Selection::Selection;

	enum : uint8_t { USE_SONG_STUTTER = 0, NORMAL, REVERSED, PING_PONG };

	deluge::vector<std::string_view> getOptions(OptType optType = OptType::FULL) override {
		using namespace deluge::l10n;

		deluge::vector<std::string_view> result;

		if (!soundEditor.currentModControllable->isSong()) {
			result.push_back(l10n::getView(optType == OptType::SHORT ? String::STRING_FOR_USE_SONG_SHORT
			                                                         : String::STRING_FOR_USE_SONG));
		}
		result.push_back(l10n::getView(String::STRING_FOR_FORWARD));
		result.push_back(l10n::getView(String::STRING_FOR_REVERSED));
		result.push_back(l10n::getView(String::STRING_FOR_PING_PONG));

		return result;
	}

	void readCurrentValue() override {
		auto* stutter = &soundEditor.currentModControllable->stutterConfig;
		if (stutter->useSongStutter) {
			this->setValue(USE_SONG_STUTTER);
		}
		else if (stutter->reversed) {
			this->setValue(REVERSED);
		}
		else if (stutter->pingPong) {
			this->setValue(PING_PONG);
		}
		else {
			this->setValue(NORMAL);
		}
	}

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		int32_t option = this->getValue();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					applyOptionToStutterConfig(option, soundDrum->stutterConfig);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			applyOptionToStutterConfig(option, soundEditor.currentModControllable->stutterConfig);
		}
	}

	static void applyOptionToStutterConfig(uint8_t option, StutterConfig& stutter) {
		stutter.useSongStutter = option == USE_SONG_STUTTER;
		stutter.reversed = option == REVERSED;
		stutter.pingPong = option == PING_PONG;

		if (stutter.useSongStutter) {
			// Copy current song settings
			stutter.quantized = currentSong->globalEffectable.stutterConfig.quantized;
			stutter.reversed = currentSong->globalEffectable.stutterConfig.reversed;
			stutter.pingPong = currentSong->globalEffectable.stutterConfig.pingPong;
		}
	}

	[[nodiscard]] int32_t getColumnSpan() const override { return 2; }
};

} // namespace deluge::gui::menu_item::stutter
