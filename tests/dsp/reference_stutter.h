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

/// @file
/// @brief The stutter as the app's Stutterer renders it (src/deluge/model/fx/stutterer.cpp): beginStutter's tape
/// setup and processStutter, verbatim but for these edits:
///   - the tape's storage is an owned vector, filled with a known sample (zero unless a spec asks otherwise) where
///     the app's comes uninitialised from the allocator;
///   - a native tape's write is writeNative, which DelayTape::write calls for a native tape (the app passes it
///     strengths it never set, which a native write never reads);
///   - the resampled play's two-tap sum is wrapping_add, shifted through uint32_t;
///   - the rate comes in per block, where the app computes it from the stutter-rate param and the tempo.
/// It also counts the modes it renders in, so a spec can show a workload reaches each.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "effects/state.h"
#include "util/fixedpoint.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace deluge::dsp::test {

/// @brief How often a stutter rendered in each mode, over every ReferenceStutter since the last reset.
struct StutterCounts {
	std::size_t record_blocks = 0;    ///< Blocks rendered recording.
	std::size_t play_blocks = 0;      ///< Blocks rendered playing.
	std::size_t native_frames = 0;    ///< Frames rendered over a native tape.
	std::size_t resampled_frames = 0; ///< Frames rendered over a resampling tape.
	std::size_t reverse_frames = 0;   ///< Frames played backwards.
	std::size_t flips = 0;            ///< Ping-pong turns at an end of the tape.
};

class ReferenceStutter {
public:
	using DelayTape = fixed::DelayTape;

	/// The counts since the last reset_counters().
	static inline StutterCounts counts{};
	static void reset_counters() { counts = {}; }

	/// @brief Begins a stutter at @p rate with @p settings: a tape of the size @p rate plays at, plus the write lag,
	/// its storage holding @p fill.
	void begin(uint32_t rate, const effects::StutterState& settings, StereoSample fill = {.l = 0, .r = 0}) {
		playing_settings_ = settings;
		current_reverse_ = playing_settings_.reversed;
		const auto [size, clamped] = DelayTape::idealSizeForRate(rate);
		const std::size_t frames = static_cast<std::size_t>(size) + static_cast<std::size_t>(DelayTape::kWriteLag);
		storage_.assign(frames, fill);
		tape_ = DelayTape{std::span<StereoSample>{storage_}, static_cast<std::size_t>(size), rate};
		if (clamped) {
			tape_.makeNativeRatePrecise();
		}
		recording_ = true;
		size_left_until_record_finished_ = static_cast<int32_t>(tape_.size());
	}

	/// @brief Renders @p audio in place at @p rate.
	void process(std::span<StereoSample> audio, uint32_t rate) {
		DelayTape& tape = tape_;

		tape.setupForRender(static_cast<int32_t>(rate));

		if (recording_) {
			counts.record_blocks += 1;
			for (StereoSample sample : audio) {
				int32_t strength1 = 0;
				int32_t strength2 = 0;

				if (tape.isNative()) {
					counts.native_frames += 1;
					tape.clearAndMoveOn();
					size_left_until_record_finished_--;
					tape.writeNative(sample);
				}
				else {
					counts.resampled_frames += 1;
					strength2 = tape.advance([&] {
						tape.clearAndMoveOn();
						size_left_until_record_finished_--;
					});
					strength1 = 65536 - strength2;
					tape.writeResampled(sample, strength1, strength2);
				}
			}

			if (size_left_until_record_finished_ < 0) {
				if (current_reverse_) {
					tape.setCurrent(tape.end() - 1);
				}
				else {
					tape.setCurrent(tape.begin());
				}
				recording_ = false;
			}
		}
		else {
			counts.play_blocks += 1;
			for (StereoSample& sample : audio) {
				int32_t strength1 = 0;
				int32_t strength2 = 0;
				counts.reverse_frames += current_reverse_ ? 1 : 0;

				if (tape.isNative()) {
					counts.native_frames += 1;
					if (current_reverse_) {
						tape.moveBack();
					}
					else {
						tape.moveOn();
					}
					sample.l = tape.current().l;
					sample.r = tape.current().r;
				}
				else {
					counts.resampled_frames += 1;
					if (current_reverse_) {
						strength2 = tape.retreat([&] { tape.moveBack(); });
					}
					else {
						strength2 = tape.advance([&] { tape.moveOn(); });
					}

					strength1 = 65536 - strength2;

					// Both cursor directions return a fraction toward the next sample.
					StereoSample* other = &tape.current() + 1;
					if (other == tape.end()) {
						other = tape.begin();
					}
					const StereoSample& from_delay1 = tape.current();
					const StereoSample& from_delay2 = *other;
					sample.l = mix(from_delay1.l, from_delay2.l, strength1, strength2);
					sample.r = mix(from_delay1.r, from_delay2.r, strength1, strength2);
				}

				if (playing_settings_.ping_pong
				    && ((current_reverse_ && &tape.current() == tape.begin())
				        || (!current_reverse_ && &tape.current() == tape.end() - 1))) {
					current_reverse_ = !current_reverse_;
					counts.flips += 1;
				}
			}
		}
	}

	/// @return Whether the tape has been recorded and is playing back.
	[[nodiscard]] bool playing() const { return tape_.isActive() && !recording_; }
	/// @return Whether playback runs backwards.
	[[nodiscard]] bool reversed() const { return current_reverse_; }
	/// @return The frames the read head has left to pass while recording (below zero once recorded).
	[[nodiscard]] int32_t frames_left() const { return size_left_until_record_finished_; }
	[[nodiscard]] const DelayTape& tape() const { return tape_; }

private:
	/// @return The two taps @p a and @p b weighed by @p strength1 and @p strength2 (16.16, summing to 65536), Q31.
	[[nodiscard]] static q31 mix(q31 a, q31 b, int32_t strength1, int32_t strength2) {
		const q31 sum =
		    wrapping_add(multiply_32x32_rshift32(a, strength1 << 14), multiply_32x32_rshift32(b, strength2 << 14));
		return static_cast<q31>(static_cast<uint32_t>(sum) << 2);
	}

	std::vector<StereoSample> storage_;
	DelayTape tape_;
	effects::StutterState playing_settings_{};
	bool current_reverse_ = false;
	bool recording_ = false;
	int32_t size_left_until_record_finished_ = 0;
};

} // namespace deluge::dsp::test
