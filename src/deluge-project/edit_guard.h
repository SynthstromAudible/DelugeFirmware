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

#pragma once

#include <cstdint>

/// @brief Exclude the render, unless the caller is the render itself, before a structural edit to a container the
/// render may read; supplied by the host at link time.
/// @return Whether it excluded, and so must be exited.
extern "C" bool deluge_model_edit_enter() noexcept;

/// @brief End an exclusion begun by deluge_model_edit_enter, if @p entered.
extern "C" void deluge_model_edit_exit(bool entered) noexcept;

/// @brief Report one structural edit to the host's probe, which counts any made while the render could read the
/// container mid-edit; supplied by the host at link time.
/// @param kind What was edited: a deluge::project::ModelEdit.
extern "C" void deluge_model_note_structural_edit(uint8_t kind) noexcept;

namespace deluge::project {

/// @brief What a structural edit was made to, for the host's probe.
enum class ModelEdit : uint8_t {
	NODES,       ///< one op on an OrderedPosVector (notes, note rows, automation nodes, clip instances)
	PARAM_STORE, ///< a store the render resolves lanes through: the clip and backed-param maps, a manager's lanes
	NODE_KERNEL, ///< a compound automation-node edit (a param_edit.h kernel): its node ops and the index arithmetic
	             ///< between them
};

/// @brief Brackets one structural edit (insert, erase, reorder, reallocate, hand-over) of a model container the
/// render may read: the render reads automation and note data straight out of the model, and on the device it
/// preempts every other context at any instruction. Guards nest: a compound edit holds one across all its steps, so
/// the render never sees it half done.
class EditGuard {
public:
	explicit EditGuard(ModelEdit kind = ModelEdit::NODES) noexcept : entered_(deluge_model_edit_enter()) {
		deluge_model_note_structural_edit(static_cast<uint8_t>(kind));
	}
	~EditGuard() { deluge_model_edit_exit(entered_); }
	EditGuard(const EditGuard&) = delete;
	EditGuard& operator=(const EditGuard&) = delete;

private:
	bool entered_;
};

} // namespace deluge::project
