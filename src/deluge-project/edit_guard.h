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

/// @brief Exclude the render, unless the caller is the render itself, before a structural edit to a container the
/// render may read; supplied by the host at link time.
/// @return Whether it excluded, and so must be exited.
extern "C" bool deluge_model_edit_enter() noexcept;

/// @brief End an exclusion begun by deluge_model_edit_enter, if @p entered.
extern "C" void deluge_model_edit_exit(bool entered) noexcept;

/// @brief Report one structural edit to the host's probe, which counts any made while the render could read the
/// container mid-edit; supplied by the host at link time.
extern "C" void deluge_model_note_structural_edit() noexcept;

namespace deluge::project {

/// @brief Brackets one structural edit (insert, erase, reorder, reallocate, hand-over) of a model container the
/// render may read: the render reads automation and note data straight out of the model, and on the device it
/// preempts every other context at any instruction.
class EditGuard {
public:
	EditGuard() noexcept : entered_(deluge_model_edit_enter()) { deluge_model_note_structural_edit(); }
	~EditGuard() { deluge_model_edit_exit(entered_); }
	EditGuard(const EditGuard&) = delete;
	EditGuard& operator=(const EditGuard&) = delete;

private:
	bool entered_;
};

} // namespace deluge::project
