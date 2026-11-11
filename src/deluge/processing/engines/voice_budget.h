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

#include <algorithm>
#include <array>
#include <cstdint>

namespace deluge::audio {

/// @brief Predicted cycles for one voice for one 128-frame block, by VoiceKind (oscillator, sample, FM, ring
/// modulation): what a Sound is costed at until it has measured its own voices. The stream bench's largest per-kind
/// figure over the rig songs (Long 19, Cordae, Kitchen Clave, each with Glitchy_2 loaded while playing), rounded up.
/// None of them ring-modulates, so ring modulation takes the largest.
inline constexpr std::array<uint32_t, 4> kPriorCyclesPerVoiceBlock = {37'000, 90'000, 15'000, 90'000};

/// @brief What one of a Sound's voices costs for one 128-frame block, in CPU cycles, measured from its renders and
/// smoothed.
///
/// Renders accumulate until they hold four blocks of one voice, so a short window's per-call overhead does not
/// dominate a reading; each such fold moves the reading halfway to it.
class VoiceCost {
public:
	static constexpr uint32_t kBlockFrames = 128;
	static constexpr uint32_t kFoldFrames = 4 * kBlockFrames;

	/// @brief Notes one render of the Sound's voices: they took @p cycles over @p voice_frames (voices × frames).
	void add_render(uint32_t cycles, uint32_t voice_frames) {
		if (voice_frames == 0) {
			return;
		}
		accCycles_ += cycles;
		accFrames_ += voice_frames;
		if (accFrames_ < kFoldFrames) {
			return;
		}
		const auto sample = static_cast<uint32_t>(accCycles_ * kBlockFrames / accFrames_);
		perBlock_ = measured_ ? (perBlock_ + sample) / 2 : sample;
		measured_ = true;
		accCycles_ = 0;
		accFrames_ = 0;
	}

	/// @brief Cycles one voice costs for a 128-frame block, or @p prior before any measurement.
	[[nodiscard]] uint32_t per_block(uint32_t prior) const { return measured_ ? perBlock_ : prior; }
	[[nodiscard]] bool measured() const { return measured_; }

private:
	uint64_t accCycles_ = 0;
	uint32_t accFrames_ = 0;
	uint32_t perBlock_ = 0;
	bool measured_ = false;
};

/// @brief The cycles one block's render may spend, against what the last blocks spent and what has been admitted
/// since.
///
/// The load is the measured render, peak-held and falling by a sixteenth a block, so a quiet block between busy ones
/// does not admit voices the busy ones cannot render. A voice admitted counts until the next block's measurement
/// includes it. With no cycle counter (rate 0), it admits everything and reads 0.
class VoiceBudget {
public:
	/// @brief Sets the counter's rate and the share of a block period the render may spend, in percent.
	void configure(uint32_t cycle_hz, uint32_t percent) {
		cycleHz_ = cycle_hz;
		blockCycles_ = static_cast<uint32_t>(static_cast<uint64_t>(cycle_hz) * kBlockFrames / kSampleRate);
		capacity_ = static_cast<uint32_t>(static_cast<uint64_t>(blockCycles_) * percent / 100);
	}

	/// @brief Notes the block just rendered: it took @p cycles. Admissions start again from the held load.
	void end_block(uint32_t cycles) {
		last_ = cycles;
		held_ = std::max(cycles, held_ - held_ / 16);
		committed_ = held_;
	}

	/// @brief Whether a voice costing @p cost cycles fits this block; if it does, it is counted.
	[[nodiscard]] bool admit(uint32_t cost) {
		if (!enabled()) {
			return true;
		}
		if (static_cast<uint64_t>(committed_) + cost > capacity_) {
			return false;
		}
		committed_ += cost;
		return true;
	}

	/// @brief Counts a voice started without admission (after a steal), so later admissions see it.
	void commit(uint32_t cost) {
		committed_ = static_cast<uint32_t>(std::min<uint64_t>(0xFFFF'FFFFu, static_cast<uint64_t>(committed_) + cost));
	}

	/// @brief The held load, as the samples of real time a 128-frame block takes (128 is the whole period).
	[[nodiscard]] int32_t held_samples() const { return toSamples(held_); }
	/// @brief The last block's render, likewise.
	[[nodiscard]] int32_t last_samples() const { return toSamples(last_); }
	[[nodiscard]] bool enabled() const { return cycleHz_ != 0; }
	[[nodiscard]] uint32_t capacity() const { return capacity_; }

private:
	static constexpr uint32_t kBlockFrames = 128;
	static constexpr uint32_t kSampleRate = 44100;

	[[nodiscard]] int32_t toSamples(uint32_t cycles) const {
		return cycleHz_ == 0 ? 0 : static_cast<int32_t>(static_cast<uint64_t>(cycles) * kSampleRate / cycleHz_);
	}

	uint32_t cycleHz_ = 0;
	uint32_t blockCycles_ = 0;
	uint32_t capacity_ = 0;
	uint32_t held_ = 0;
	uint32_t last_ = 0;
	uint32_t committed_ = 0;
};

/// @brief How many voices the backstop culls, and how.
struct CullPlan {
	int32_t kill = 0;          ///< Stopped within the block (may click).
	int32_t terminate = 0;     ///< Fast release.
	int32_t force_release = 0; ///< Soft: released, or its release sped up.
};

/// @brief The backstop's cull for a block whose render ran past its period.
/// @param over How far the last block's render ran past the period, in samples of a 128-frame block (negative: it
///        fitted).
/// @param last_over The same for the block before.
/// @param overran Whether the board counted an overrun since the last block; read as a hard overshoot.
/// @param voices Synth voices and audio-clip voices sounding.
/// @param min_voices Below this many a cull no longer helps; only a far overshoot culls under it.
[[nodiscard]] constexpr CullPlan plan_cull(int32_t over, int32_t last_over, bool overran, int32_t voices,
                                           int32_t min_voices = 7) {
	CullPlan plan;
	if (overran) {
		over = std::max(over, 32);
	}
	if (voices > min_voices) {
		if (over >= 32) {
			const int32_t n = std::min(over >> 4, voices - min_voices);
			const int32_t hard = n - 1;
			plan.kill = hard / 2;
			plan.terminate = hard - plan.kill;
			plan.force_release = 1;
		}
		else if (over >= 0 && last_over > 0 && over >= last_over) {
			plan.force_release = 1;
		}
	}
	else if (over >= 40) {
		plan.force_release = 1;
	}
	return plan;
}

} // namespace deluge::audio
