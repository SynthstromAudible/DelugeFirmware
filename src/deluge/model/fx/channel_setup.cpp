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

#include "model/fx/channel_setup.h"
#include "libdeluge/system.h" // AudioExcludedSection
#include "util/structural_edit_probe.h"

namespace deluge::model {

void initChannelSettings(EffectsBinding& binding, const ChainView& view, effects::ChainCore& core,
                         effects::Filter& filter) {
	effects::EffectStates next = binding.effects();
	next.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	binding.store(next);
	binding.project(core, 0, view.modFXParam());
	filter.reset();
	next = binding.effects();
	next.mod_fx.type = ModFXType::NONE;
	binding.store(next);
	binding.project(core, 0, view.modFXParam());
}

void releaseDelayMemory(effects::Delay& delay) {
	effects::DelayChange change = delay.prepare(effects::DelayType::OFF);
	{
		AudioExcludedSection section; // the render takes from the arena
		delay.commit(effects::DelayType::OFF, change);
		noteStructuralEdit(StructuralContainer::DELAY_TAPE);
	}
	delay.finish(change);
}

} // namespace deluge::model
