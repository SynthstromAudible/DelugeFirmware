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

/// libdeluge/card.h's C++-side fallback. The real implementation is the Rust Embassy BSP's
/// `card.rs`, which bumps the shared card generation and clears each loader's tables; this file
/// exists only so BSPs without those loaders (legacy/host-cooperative sim, rza1) still resolve the
/// symbol and can call it unconditionally.
///
/// Filed in storage/ rather than storage/audio/ beside audio_load_abi.cpp, because card.h is
/// card-scoped, not audio-scoped: a card swap invalidates every loader at once, which is the whole
/// reason the generation is shared. file_prefix_abi.cpp, the other non-audio channel, is its
/// neighbour here.
///
/// `noexcept`, like every function in audio_load_abi.cpp: the repo's allocators throw
/// `deluge::exception::BAD_ALLOC` (operators.cpp), and letting anything unwind out of an
/// `extern "C"` frame into a Rust caller is undefined behaviour -- on this target it presents as
/// `std::terminate()` aborting silently. There is no body here to throw, and the marker keeps that
/// true if one is ever added.

#include "libdeluge/card.h"

// Weak fallback: BSPs without the Rust loaders resolve this and do nothing. The Rust Embassy BSP
// overrides it in card.rs.
extern "C" __attribute__((weak)) void deluge_card_flush() noexcept {
	// No card-derived caches on this BSP/config, so nothing was ever queued, parsed or published and
	// there is nothing a card swap could make stale. Unconditionally callable is the point: the
	// ejection path calls this without first asking any loader's "am I active?" selector, because a
	// flush that is skipped on the wrong build is a silent wrong-file load, while one that runs on a
	// build with no tables costs a call.
}
