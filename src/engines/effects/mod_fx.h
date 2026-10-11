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
#include "dsp-next/fixed/mod_fx/mod_fx_kernel.h"
#include "dsp-next/fixed/noise/cong_noise.h"
#include "engines/effects/words.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::effects {

/// @brief How an owner gives its mod FX memory: the app's heap, or a test's.
struct RingAllocator {
	void* (*allocate)(std::size_t bytes); ///< Null when out of memory.
	void (*release)(void* block);
};

/// @brief A type change's ring: the one allocated before the section, and the one taken back inside it.
struct RingChange {
	dsp::StereoSample* acquired = nullptr; ///< Allocated and zeroed for a ring type when none was held.
	dsp::StereoSample* released = nullptr; ///< Taken back by commit; freed by finish.
};

/// @brief What the owner hands render each block.
struct ModFxInputs {
	ModFXType active; ///< The type in effect this block: NONE in a chain's soft off.
	int32_t rate;     ///< The rate param's final value, UQ0.32 per frame.
	int32_t depth;    ///< The depth param's final value, Q31.
	int32_t offset;   ///< The offset knob, Q31 signed.
	int32_t feedback; ///< The feedback knob, Q31 signed.
	bool stereo;      ///< Whether the render is in stereo.
	bool sound;       ///< Whether any sound came in this block.
};

/// @return Whether @p type runs over a ring: chorus, stereo chorus, dimension, flanger or warble.
[[nodiscard]] constexpr bool is_ring_type(ModFXType type) {
	switch (type) {
	case ModFXType::CHORUS:
	case ModFXType::CHORUS_STEREO:
	case ModFXType::DIMENSION:
	case ModFXType::FLANGER:
	case ModFXType::WARBLE:
		return true;
	default:
		return false;
	}
}

/// @return The kernel mode for @p type (not NONE or GRAIN) in stereo or mono.
[[nodiscard]] constexpr dsp::fixed::ModFx<dsp::fixed::CongNoise>::Mode mod_fx_mode(ModFXType type, bool stereo) {
	using Mode = dsp::fixed::ModFx<dsp::fixed::CongNoise>::Mode;
	switch (type) {
	case ModFXType::DIMENSION:
		return Mode::DIMENSION;
	case ModFXType::FLANGER:
		return stereo ? Mode::FLANGER : Mode::FLANGER_MONO;
	case ModFXType::WARBLE:
		return stereo ? Mode::WARBLE : Mode::WARBLE_MONO;
	case ModFXType::PHASER:
		return Mode::PHASER;
	default: // CHORUS, CHORUS_STEREO
		return stereo ? Mode::CHORUS : Mode::CHORUS_MONO;
	}
}

/// @brief An owner's mod FX: the dsp-next mod FX kernel, the 4 KB ring it reads through an allocator, the soft off and
/// the pass-through of a ring type that holds no ring.
///
/// A ring type's change runs in three steps around the owner's audio-excluded section: @ref prepare allocates, @ref
/// commit swaps the kernel's ring inside the section, @ref finish frees. A change to a type that needs a ring while one
/// is held changes nothing.
class ModFx {
public:
	using Kernel = dsp::fixed::ModFx<dsp::fixed::CongNoise>;

	/// @brief Takes the allocator the ring comes from and goes back to.
	explicit ModFx(RingAllocator allocator);
	/// @brief Frees the ring.
	~ModFx();
	ModFx(const ModFx&) = delete;
	ModFx& operator=(const ModFx&) = delete;

	/// @brief Interaction tier, before the section: allocates and zeroes the ring @p type needs when none is held.
	/// A failed allocation leaves no ring, and the type's effect then passes through.
	[[nodiscard]] RingChange prepare(ModFXType type);
	/// @brief Inside the owner's AudioExcludedSection: hands the kernel the acquired ring, or takes the held one back
	/// when @p type needs none.
	void commit(ModFXType type, RingChange& change);
	/// @brief After the section: frees what commit took back, and an acquired ring commit did not take.
	void finish(RingChange& change);

	/// @brief Render: the mod FX over @p block in place, multiplying @p post_fx_volume (Q31) by the type's gain.
	void render(std::span<dsp::StereoSample> block, const ModFxInputs& in, int32_t& post_fx_volume);
	/// @brief Advances the LFO by @p frames at @p rate (UQ0.32 per frame) while the owner skips rendering.
	void tick(int32_t frames, int32_t rate);
	/// @brief Clears the ring when one is held, otherwise the phaser's memories.
	void clearMemory();
	/// @return Whether a ring is held.
	[[nodiscard]] bool holdsRing() const { return mono_.state().ring != nullptr; }
	/// @return The kernel's state, for specs.
	[[nodiscard]] const Kernel::State& state() const { return mono_.state(); }

private:
	RingAllocator allocator_;
	dsp::Mono<Kernel> mono_;
	std::optional<Kernel::Settings> settings_; ///< The last configured settings.
	bool soft_off_ = false;                    ///< Whether the ring was left to fill with stale audio.
};

} // namespace deluge::effects
