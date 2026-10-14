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

/// @file
/// @brief The scenes' shipped op dispatcher.

namespace deluge::scenes {

/// @brief Run @p op on the caller's stack.
///
/// The scenes run on the interaction tier, which is where the legacy UI does the same work: a
/// browser's directory read, as `Browser` does it, and the slicer's audition and commit, whose
/// `SliceOps` calls are the interaction tier's to make. Nothing here runs on the audio tier.
///
/// The ops' seams keep a dispatcher pointer so a spec can install one that defers the op and
/// observe the in-flight state. A dispatcher returns whether the op ran or will run exactly once.
///
/// @param op  The op to run.
/// @param ctx Its context pointer.
/// @return True: the op has run.
inline bool run_inline(void (*op)(void*), void* ctx) {
	op(ctx);
	return true;
}

} // namespace deluge::scenes
