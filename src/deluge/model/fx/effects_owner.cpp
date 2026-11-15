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

#include "model/fx/effects_owner.h"
#include "effects/plugin.h"
#include "libdeluge/system.h" // AudioExcludedSection
#include "model/fx/stutter_gesture.h"
#include "model/mod_controllable/delay_type.h"
#include "model/song/song.h"
#include "util/structural_edit_probe.h"

#include <cstddef>
#include <optional>

namespace deluge::model {

void projectEffects(EffectsOwner owner) {
	const effects::DelayState& delay = owner.effects().delay;
	const std::size_t delayReserve = (delay.type != effects::DelayType::OFF && currentSong != nullptr)
	                                     ? delayReserveFrames(*currentSong, *owner.core, delay, owner.sound != nullptr)
	                                     : 0;
	owner.binding->project(*owner.core, delayReserve,
	                       owner.view != nullptr ? std::optional{owner.view->modFXParam()} : std::nullopt);
}

void storeEffects(EffectsOwner owner, const effects::EffectStates& next) {
	owner.binding->store(next);
	projectEffects(owner);
}

void bindEffects(EffectsOwner owner, effects::EffectStates* record) {
	owner.binding->bind(record);
	projectEffects(owner);
}

void setChainView(EffectsOwner owner, ModFXParam param, FilterType type) {
	owner.view->write(param, type);
	projectEffects(owner);
}

void bindChain(EffectsOwner owner, project::FxChain& chain) {
	if (owner.view->bound() == &chain) {
		return;
	}
	chain.effects = plugin::StateHandle::of<effects::DelugeEffects>(owner.effects());
	owner.view->bind(chain);
	bindEffects(owner, chain.effects.as<effects::DelugeEffects>());
}

void unbindChain(EffectsOwner owner) {
	if (owner.view->bound() == nullptr) {
		return;
	}
	owner.view->unbind();
	bindEffects(owner, nullptr); // copies the record's effects in
}

void clearModFXMemory(EffectsOwner owner) {
	const ModFXType type = owner.binding->snapshot().effects.mod_fx.type;
	if (type == ModFXType::GRAIN) {
		owner.core->granular().clear();
	}
	else if (type != ModFXType::NONE) {
		owner.core->mod_fx().clearMemory();
	}
}

void chainWontRender(EffectsOwner owner) {
	{
		AudioExcludedSection section; // the render gives tapes back to the same arena
		owner.core->delay().discardBuffers();
		noteStructuralEdit(StructuralContainer::DELAY_TAPE);
	}
	endStutter(nullptr);
}

} // namespace deluge::model
