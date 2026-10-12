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

/// @brief A type change's allocations: the ones made before the section, and the ring taken back inside it.
struct GranularBlockChange {
	void* acquired_state = nullptr; ///< The kernel's State, allocated when the owner has none.
	void* acquired_ring = nullptr;  ///< A zeroed ring, allocated for GRAIN when none was held.
	void* released_ring = nullptr;  ///< A ring taken back by commit, or an acquired one it did not use.
};

/// @brief What entering GRAIN does to the effect's State: the owner kinds differ.
enum class EnterGrain : uint8_t {
	CLEAR, ///< Stop the grains and begin the first pass again: a sound, whose change to GRAIN clears the effect.
	KEEP,  ///< Only take the ring up afresh: a clip or master chain, which keeps the grains and memories.
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

/// @brief An owner's granular effect: the dsp-next Granular kernel's State, which lives as long as the owner once it
/// has entered GRAIN, and the 65,536-frame ring (about 512 KB), which is held while the owner's type is GRAIN.
///
/// Both come from the allocator. The State (about 350 bytes) is allocated on the first entry into GRAIN and kept until
/// the owner is destroyed, so the grains, the wet low-pass memories, the first-pass flag and the shutdown countdown
/// survive a stay on another type, as the processor's do today. The ring is zeroed once when acquired, so the grains
/// read silence until it is written, and is freed on leaving GRAIN. An owner that has never used GRAIN holds nothing.
///
/// A type change runs in three steps around the owner's audio-excluded section: @ref prepare allocates, @ref commit
/// swaps inside it, @ref finish frees. Leaving GRAIN restarts the effect and frees the ring; entering takes up a new
/// ring and, for an owner built with EnterGrain::CLEAR, clears the effect. A chain's new State renders nothing until
/// its first sounding block, as its processor is not made before then; a sound's renders from the type change. A
/// chain's soft off, which leaves the type unchanged, calls @ref startSkippingRendering and keeps the ring.
class Granular {
public:
	using Kernel = dsp::fixed::Granular<dsp::fixed::CongNoise>;

	/// @brief Takes the allocator the blocks come from and go back to.
	/// @param enter What entering GRAIN does to the State: CLEAR for a sound, KEEP for a chain.
	Granular(RingAllocator allocator, EnterGrain enter);
	/// @brief Frees the State and the ring.
	~Granular();
	Granular(const Granular&) = delete;
	Granular& operator=(const Granular&) = delete;

	/// @brief Interaction tier, before the section: allocates the State when the owner has none, and a zeroed ring when
	/// @p type is GRAIN and none is held. A failed allocation leaves the effect passing through.
	[[nodiscard]] GranularBlockChange prepare(ModFXType type);
	/// @brief Inside the owner's AudioExcludedSection: takes up the acquired State and ring, or restarts the effect and
	/// hands the ring back when @p type is not GRAIN. An acquired ring is not taken while one is held: it moves to
	/// released_ring.
	void commit(ModFXType type, GranularBlockChange& change);
	/// @brief After the section: frees what commit took back, and what it did not take.
	void finish(GranularBlockChange& change);

	/// @brief Render: the granular effect over @p block in place while sound comes in or the shutdown countdown runs.
	/// Scales @p post_fx_volume (Q31) by 1/√2 then, adds the reverb feed into @p reverb_send (one value per frame),
	/// and restarts the effect when the countdown ends. Passes through when no ring is held.
	/// @details The owner calls this every rendered block, with all four knobs, as today's processGrainFX is called.
	void render(std::span<dsp::StereoSample> block, const GranularInputs& in, int32_t& post_fx_volume,
	            std::span<dsp::q31> reverb_send);
	/// @brief Stops the grains and begins the first pass again, as today's clearGrainFXBuffer; no effect without a
	/// State.
	void clear();
	/// @brief The owner has gone idle or soft off: the ring is taken up afresh and grains wait until it is half full
	/// again, those sounding going on. The ring is kept. No effect without a State.
	void startSkippingRendering();
	/// @return The frames left before the effect restarts: the laps left times the ring's length, as today's
	/// getSamplesToShutdown; 0 without a State.
	[[nodiscard]] int32_t samplesToShutdown() const;
	/// @return Whether a ring is held.
	[[nodiscard]] bool holdsBlock() const { return ring_ != nullptr; }
	/// @return The kernel's state, or null before the first entry into GRAIN; for specs.
	[[nodiscard]] const Kernel::State* state() const;

private:
	using Mono = dsp::Mono<Kernel>;

	/// @brief Ends @p state's life as a Mono and hands it back to @p allocator.
	static void destroy_state(RingAllocator allocator, void* state);

	/// @brief Event: the effect waits for its first sound, rendering nothing, as a chain's processor, which does not
	/// exist before it, does.
	static void idle(Kernel::State& state);

	RingAllocator allocator_;
	EnterGrain enter_;
	Mono* mono_ = nullptr;              ///< The State block; kept once allocated.
	dsp::StereoSample* ring_ = nullptr; ///< The ring block, while the type is GRAIN.
};

} // namespace deluge::effects
