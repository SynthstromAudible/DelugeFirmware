/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "model/instrument/rendering_drums.h"
#include "definitions.h"
#include "libdeluge/system.h"
#include "processing/sound/sound_drum.h"
#include "util/structural_edit_probe.h"

void RenderingDrums::add(SoundDrum* drum) {
	if (contains(drum)) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E463");
#endif
		return;
	}
	AudioExcludedSection section; // the render walks this list
	deluge::noteStructuralEdit(deluge::StructuralContainer::RENDERING_DRUMS);
	auto it = list_.begin();
	while (it != list_.end() && &*it < drum) {
		++it;
	}
	list_.insert(it, *drum);
}

void RenderingDrums::remove(SoundDrum* drum) {
	if (!contains(drum)) {
		return;
	}
	AudioExcludedSection section; // the render walks this list
	deluge::noteStructuralEdit(deluge::StructuralContainer::RENDERING_DRUMS);
	list_.unlinkElement(static_cast<Link&>(*drum));
}

bool RenderingDrums::contains(const SoundDrum* drum) const {
	return static_cast<const Link*>(drum)->is_linked();
}

SoundDrum* RenderingDrums::drumOf(Link* link) {
	return static_cast<SoundDrum*>(link);
}
