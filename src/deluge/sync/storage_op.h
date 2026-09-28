/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

/// deluge::sync — RAII scopes that replace the cooperative-scheduling glue with
/// explicit primitives over the libdeluge BSP seam. See
/// docs/superpowers/specs/2026-07-16-cooperative-glue-retirement-design.md.

namespace deluge::sync {

/// @brief RAII scope for a long storage (SD/card) operation.
///
/// While alive, permits the whitelisted user-action handlers to run even though the
/// card routine is active (they otherwise defer with
/// `ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE`). The prior permit state is restored
/// on destruction, so nested scopes compose correctly.
///
/// @note `StorageOp` implements only this permit role.
class StorageOp {
public:
	StorageOp();
	~StorageOp();

	StorageOp(const StorageOp&) = delete;
	StorageOp& operator=(const StorageOp&) = delete;
	StorageOp(StorageOp&&) = delete;
	StorageOp& operator=(StorageOp&&) = delete;

private:
	bool prev_permit_;
};

/// @brief Whether user-action handlers may run during the in-progress storage operation.
///
/// True exactly while a `StorageOp` is alive. The UI action handlers query this to decide
/// whether to proceed or defer with `ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE` when the
/// card routine is active.
bool user_actions_permitted();

} // namespace deluge::sync
