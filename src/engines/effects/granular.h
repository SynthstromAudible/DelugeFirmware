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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/granular/granular_kernel.h"
#include "dsp-next/fixed/noise/cong_noise.h"
#include "engines/effects/mod_fx.h" // RingAllocator
#include "engines/effects/words.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief A type change's block: the one allocated before the section, and the one taken back inside it.
struct GranularBlockChange {
	void* acquired = nullptr; ///< Allocated, zeroed and readied for GRAIN when none was held.
	void* released = nullptr; ///< Taken back by commit; freed by finish.
};

/// @brief What the owner hands render each block.
struct GranularInputs {
	int32_t rate;         ///< The rate knob, Q31 signed.
	int32_t mix;          ///< The mix knob, Q31 signed.
	int32_t density;      ///< The density knob, Q31 signed.
	int32_t randomness;   ///< The pitch-randomness knob, Q31 signed.
	dsp::q31 send_amount; ///< The reverb send amount, Q31.
	bool sound;           ///< Whether any sound came in this block.
};

/// @brief An owner's granular effect: the dsp-next Granular kernel and its ring in one block from an allocator, held
/// while the owner's type is GRAIN.
///
/// The block holds the kernel's state with its 65,536-frame ring (about 512 KB), zeroed once when it is acquired, so
/// its grains read silence until the ring is written. An owner not on GRAIN holds nothing and pays nothing.
///
/// A type change runs in three steps around the owner's audio-excluded section: @ref prepare allocates, @ref commit
/// swaps the block in or out inside it, @ref finish frees. Leaving GRAIN frees the block with the state it holds, so
/// entering GRAIN always begins on a cleared effect, as a sound's change to GRAIN clears today's. A chain's soft off
/// keeps the block: it calls @ref startSkippingRendering, and the effect restarts without clearing its grains, as a
/// chain's re-acquiring does today.
class Granular {
public:
	using Kernel = dsp::fixed::Granular<dsp::fixed::CongNoise>;

	/// @brief Takes the allocator the block comes from and goes back to.
	explicit Granular(RingAllocator allocator);
	/// @brief Frees the block.
	~Granular();
	Granular(const Granular&) = delete;
	Granular& operator=(const Granular&) = delete;

	/// @brief Interaction tier, before the section: allocates and readies the block @p type needs when none is held.
	/// A failed allocation leaves no block, and the effect then passes through.
	[[nodiscard]] GranularBlockChange prepare(ModFXType type);
	/// @brief Inside the owner's AudioExcludedSection: takes up the acquired block, or hands back the held one when
	/// @p type is not GRAIN. An acquired block is not taken while one is held: it moves to released.
	void commit(ModFXType type, GranularBlockChange& change);
	/// @brief After the section: frees what commit took back, and an acquired block commit did not take.
	void finish(GranularBlockChange& change);

	/// @brief Render: the granular effect over @p block in place while sound comes in or the shutdown countdown runs.
	/// Scales @p post_fx_volume (Q31) by 1/√2 then, adds the reverb feed into @p reverb_send (one value per frame),
	/// and restarts the effect when the countdown ends. Passes through when no block is held.
	/// @details The owner calls this every rendered block, with all four knobs, as today's processGrainFX is called.
	void render(std::span<dsp::StereoSample> block, const GranularInputs& in, int32_t& post_fx_volume,
	            std::span<dsp::q31> reverb_send);
	/// @brief Stops the grains and begins the first pass again, as today's clearGrainFXBuffer; no effect without a
	/// block.
	void clear();
	/// @brief The owner has gone idle or soft off: the ring is taken up afresh and grains wait until it is half full
	/// again, those sounding going on. No effect without a block.
	void startSkippingRendering();
	/// @return The frames left before the effect restarts: the laps left times the ring's length, as today's
	/// getSamplesToShutdown; 0 without a block.
	[[nodiscard]] int32_t samplesToShutdown() const;
	/// @return Whether a block is held.
	[[nodiscard]] bool holdsBlock() const { return held_ != nullptr; }
	/// @return The kernel's state, or null without a block; for specs.
	[[nodiscard]] const Kernel::State* state() const;

private:
	struct Held; ///< The kernel with its ring, one allocation.

	/// @brief Ends @p block's life as a Held and hands it back to @p allocator.
	static void destroy(RingAllocator allocator, void* block);

	RingAllocator allocator_;
	Held* held_ = nullptr;
};

} // namespace deluge::effects
