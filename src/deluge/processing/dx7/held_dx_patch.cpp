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

#include "processing/dx7/held_dx_patch.h"

#include "memory/memory_allocator_interface.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"

#include <algorithm>

HeldDxPatch::~HeldDxPatch() {
	if (patch_ != nullptr) {
		patch_->~DxPatch();
		delugeDealloc(patch_);
	}
}

DxPatch* HeldDxPatch::ensure() {
	if (patch_ == nullptr) {
		DxEngine* engine = getDxEngine();
		if (engine == nullptr) {
			return nullptr;
		}
		patch_ = engine->newPatch();
	}
	return patch_;
}

std::optional<deluge::instruments::classic::DxPatchState> HeldDxPatch::state() const {
	if (patch_ == nullptr) {
		return std::nullopt;
	}
	deluge::instruments::classic::DxPatchState held;
	std::ranges::copy(patch_->params, held.params.begin());
	held.engine_mode = patch_->engineMode;
	held.random_detune = patch_->random_detune;
	return held;
}

bool HeldDxPatch::assign(const deluge::instruments::classic::DxPatchState& from) {
	DxPatch* patch = ensure();
	if (patch == nullptr) {
		return false;
	}
	std::ranges::copy(from.params, patch->params.begin());
	// The engine follows the bytes: an auto patch on algorithm 4 or 6 with feedback renders in MkI
	patch->setEngineMode(from.engine_mode);
	patch->random_detune = from.random_detune;
	changed = true;
	return true;
}
