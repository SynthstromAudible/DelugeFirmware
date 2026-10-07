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

#include "model/edits/change_effect.h"

#include "io/debug/log.h"
#include "model/edits/set_effect_setting.h"
#include "model/edits/set_engine_setting.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

#include <optional>
#include <string>

namespace deluge::edits {

void changeEffect(ModControllableAudio& owner, std::string_view path, deluge::serialization::Value value) {
	Error error = Error::NONE;
	::Sound* sound = owner.asSound();
	const std::optional<deluge::project::SlotAddress> slot =
	    sound != nullptr ? deluge::bridge::slotAddressOf(*sound) : std::nullopt;
	if (slot && currentSong != nullptr && deluge::bridge::soundAt(*currentSong, *slot) == sound) {
		Context ctx{*currentSong, currentSong->project};
		SetEngineSetting edit{*slot, std::string(path), value};
		error = apply(ctx, edit);
	}
	else if (const std::optional<deluge::project::ChainAddress> chain =
	             currentSong != nullptr && sound == nullptr ? deluge::bridge::chainAddressOf(owner, *currentSong)
	                                                        : std::nullopt) {
		Context ctx{*currentSong, currentSong->project};
		SetEffectSetting edit{*chain, std::string(path), value};
		error = apply(ctx, edit);
	}
	else {
		// An owner outside the song's chains (a sound not bound, a preview) holds its effects itself.
		error = deluge::bridge::applyEffectSetting(owner, path, value);
	}
	if (error != Error::NONE) {
		// A gesture offers only what the setting takes; a refusal here is a gesture and schema out of step.
		D_PRINTLN("effect setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

} // namespace deluge::edits
