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
#include <functional>

void RenderingDrums::add(SoundDrum* drum) {
	// Whether the drum is in is decided inside the section: both tiers add and remove, and a check made before it
	// could be stale by the time the splice runs (a drum inserted twice links to itself, and the render's walk would
	// never end).
	AudioExcludedSection section; // the render walks this list
	if (contains(drum)) {
		return; // already in: another tier added it first
	}
	deluge::noteStructuralEdit(deluge::StructuralContainer::RENDERING_DRUMS);
	auto it = list_.begin();
	while (it != list_.end() && std::less<const SoundDrum*>{}(&*it, drum)) {
		++it;
	}
	list_.insert(it, *drum);
}

void RenderingDrums::remove(SoundDrum* drum) {
	AudioExcludedSection section; // the render walks this list; see add() for why the check is inside
	if (!contains(drum)) {
		return;
	}
	deluge::noteStructuralEdit(deluge::StructuralContainer::RENDERING_DRUMS);
	list_.unlinkElement(static_cast<Link&>(*drum));
}

bool RenderingDrums::contains(const SoundDrum* drum) const {
	return static_cast<const Link*>(drum)->is_linked();
}

SoundDrum* RenderingDrums::drumOf(Link* link) {
	return static_cast<SoundDrum*>(link);
}
