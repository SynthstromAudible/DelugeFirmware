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

// The model's host hooks for the device and the host app: model memory comes from the fast heap, as the app's
// fast_allocator containers' does; running out throws the app's out-of-memory exception, so the app's handlers catch
// it; and a structural edit excludes the render exactly as AudioExcludedSection does, reporting to the
// structural-edit probe.

#include "deluge-project/edit_guard.h"
#include "deluge-project/model_memory.h"
#include "libdeluge/system.h"
#include "memory/heaps.h"
#include "util/exceptions.h"
#include "util/structural_edit_probe.h"

namespace deluge::project {

[[noreturn]] void model_out_of_memory() {
	throw deluge::exception::BAD_ALLOC;
}

} // namespace deluge::project

extern "C" void* deluge_model_alloc(std::size_t bytes, std::size_t alignment) noexcept {
	return deluge::memory::alloc_fast(bytes, alignment);
}

extern "C" void deluge_model_free(void* p, std::size_t /*alignment*/) noexcept {
	deluge::memory::dealloc(p);
}

extern "C" bool deluge_model_edit_enter() noexcept {
	return deluge_audio_exclude_enter();
}

extern "C" void deluge_model_edit_exit(bool entered) noexcept {
	deluge_audio_exclude_exit(entered);
}

extern "C" void deluge_model_note_structural_edit(uint8_t kind) noexcept {
	using deluge::project::ModelEdit;
	switch (static_cast<ModelEdit>(kind)) {
	case ModelEdit::PARAM_STORE:
		deluge::noteStructuralEdit(deluge::StructuralContainer::PARAM_STORE);
		return;
	case ModelEdit::NODE_KERNEL:
		deluge::noteStructuralEdit(deluge::StructuralContainer::NODE_KERNEL);
		return;
	case ModelEdit::NODES:
		break;
	}
	deluge::noteStructuralEdit(deluge::StructuralContainer::ORDERED_POS_VECTOR);
}
