/*
 * Copyright © 2024-2025 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */
#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/fx/stutterer.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::stutter {

class ScatterModeMenu final : public Selection {
public:
	using Selection::Selection;

	deluge::vector<std::string_view> getOptions(OptType optType = OptType::FULL) override {
		using namespace deluge::l10n;

		return {
		    l10n::getView(String::STRING_FOR_SCATTER_CLASSIC), l10n::getView(String::STRING_FOR_SCATTER_BURST),
		    l10n::getView(String::STRING_FOR_SCATTER_REPEAT),  l10n::getView(String::STRING_FOR_SCATTER_TIME),
		    l10n::getView(String::STRING_FOR_SCATTER_SHUFFLE), l10n::getView(String::STRING_FOR_SCATTER_LEAKY),
		    l10n::getView(String::STRING_FOR_SCATTER_PATTERN), l10n::getView(String::STRING_FOR_SCATTER_PITCH),
		};
	}

	void readCurrentValue() override {
		// Scatter mode is always per-sound (independent of useSongStutter)
		setValue(static_cast<int32_t>(soundEditor.currentModControllable->stutterConfig.scatterMode));
	}

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		auto mode = static_cast<ScatterMode>(Selection::getValue());

		// If affect-entire button held, apply to whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->stutterConfig.scatterMode = mode;
				}
			}
		}
		else {
			soundEditor.currentModControllable->stutterConfig.scatterMode = mode;
		}
	}

	void getNotificationValue(StringBuf& valueBuf) override {
		valueBuf.append(getOptions(OptType::SHORT)[Selection::getValue()]);
	}
};

} // namespace deluge::gui::menu_item::stutter
