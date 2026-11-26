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

#include "base/panic.h"
#include "io/debug/log.h"
#include "model/edits/set_chain_view.h"
#include "model/edits/set_effect_setting.h"
#include "model/edits/set_engine_setting.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "util/functions.h"

#include <optional>
#include <string>

namespace deluge::edits {

namespace {
/// @brief @p owner's effects; an owner a gesture edits the effects of always has them.
[[nodiscard]] deluge::model::EffectsOwner effectsOf(const deluge::model::LaneOwner& owner) {
	const std::optional<deluge::model::EffectsOwner> effects = deluge::model::effects_owner_of(owner);
	if (!effects) {
		FREEZE_WITH_ERROR("E475");
	}
	return *effects;
}
} // namespace

void changeEffect(deluge::model::LaneOwner owner, std::string_view path, deluge::serialization::Value value) {
	changeEffect(effectsOf(owner), path, std::move(value));
}

void changeEffect(::Song& song, deluge::model::LaneOwner owner, std::string_view path,
                  deluge::serialization::Value value) {
	changeEffect(song, effectsOf(owner), path, std::move(value));
}

void changeEffect(deluge::model::EffectsOwner effects, std::string_view path, deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		// An owner outside any song holds its effects itself.
		if (deluge::bridge::applyEffectSetting(effects, path, value) != Error::NONE) {
			D_PRINTLN("effect setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(),
			          value.str().c_str());
		}
		return;
	}
	changeEffect(*currentSong, effects, path, std::move(value));
}

void changeEffect(::Song& song, deluge::model::EffectsOwner effects, std::string_view path,
                  deluge::serialization::Value value) {
	Error error = Error::NONE;
	::Sound* sound = effects.sound;
	const std::optional<deluge::project::SlotAddress> slot =
	    sound != nullptr ? deluge::bridge::slotAddressOf(*sound) : std::nullopt;
	if (slot && deluge::bridge::soundAt(song, *slot) == sound) {
		Context ctx{song, song.project};
		SetEngineSetting edit{*slot, std::string(path), value};
		error = apply(ctx, edit);
	}
	else if (const std::optional<deluge::project::ChainAddress> chain =
	             sound == nullptr ? deluge::bridge::chainAddressOf(effects, song) : std::nullopt) {
		Context ctx{song, song.project};
		SetEffectSetting edit{*chain, std::string(path), value};
		error = apply(ctx, edit);
	}
	else {
		// An owner outside the song's chains (a sound not bound, a preview) holds its effects itself.
		error = deluge::bridge::applyEffectSetting(effects, path, value);
	}
	if (error != Error::NONE) {
		// A gesture offers only what the setting takes; a refusal here is a gesture and schema out of step.
		D_PRINTLN("effect setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

void changeView(deluge::model::LaneOwner owner, std::string_view key, std::string_view word) {
	const deluge::model::EffectsOwner effects = effectsOf(owner);
	if (effects.view == nullptr) {
		// Only a host chain has a view; a gesture offering one on a sound is a gesture and its owner out of step.
		D_PRINTLN("view refused: %.*s on an owner without a chain view", static_cast<int>(key.size()), key.data());
		return;
	}
	if (currentSong != nullptr) {
		if (const std::optional<deluge::project::ChainAddress> chain =
		        deluge::bridge::chainAddressOf(effects, *currentSong)) {
			Context ctx{*currentSong, currentSong->project};
			SetChainView edit{*chain, std::string(key), deluge::serialization::Value::makeScalar(std::string(word))};
			if (apply(ctx, edit) != Error::NONE) {
				D_PRINTLN("view refused: %.*s", static_cast<int>(key.size()), key.data());
			}
			return;
		}
	}
	const std::string text(word);
	if (key == "modFXCurrentParam") {
		deluge::model::setChainView(effects, stringToModFXParam(text.c_str()), effects.view->filterType());
	}
	else if (key == "currentFilterType") {
		deluge::model::setChainView(effects, effects.view->modFXParam(), stringToFilterType(text.c_str()));
	}
}

} // namespace deluge::edits
