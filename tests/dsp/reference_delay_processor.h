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
/// @brief The delay's two reference orders, siblings over DelayReferenceBase's shared steps, with a test-only log of
/// every tape address they clear, read and write: ReferenceDelayProcessor, the staged order (read the whole block,
/// shape, mix, then write it all back); and FrameOrderDelayProcessor, the same steps one frame at a time, the delay
/// line's order, which `DelayProcessor`'s fused kernel renders.

#pragma once

#include "dsp-next/core/chain.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_processor.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/delay/feedback_shaper.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>
#include <vector>

namespace delay_reference {

using deluge::dsp::Chain;
using deluge::dsp::Pinned;
using deluge::dsp::Stereo;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayBlock;
using deluge::dsp::fixed::DelayBlockResult;
using deluge::dsp::fixed::DelayTape;
using deluge::dsp::fixed::FeedbackShaper;
using deluge::dsp::fixed::ShiftTracker;

/// @brief Which of the block's two tapes an access is to.
enum class TapeRole : uint8_t {
	PRIMARY,   ///< The tape read and written back.
	SECONDARY, ///< The tape grown alongside.
};

/// @brief What an access does to a tape sample.
enum class TapeAccessKind : uint8_t {
	CLEAR, ///< The read head zeroes the sample it leaves (clearAndMoveOn).
	READ,  ///< The read takes the sample (the one under the head, and the next when resampling).
	WRITE, ///< A write stores or adds into the sample.
};

/// @brief One sample of one tape touched by one frame of a block.
struct TapeAccess {
	TapeRole tape;
	TapeAccessKind kind;
	int32_t frame; ///< The block frame whose step made the access.
	int32_t index; ///< The sample's index in the tape's storage, [0, sizeIncludingExtra()).
};

/// @brief Called on every access a block makes to its tapes, in the order the block makes them.
using AddressLogger = std::function<void(const TapeAccess&)>;

/// @brief What both reference orders share: the steps they take on the tapes (`read`, `mix`, `writePrimary` and
/// `writeSecondary`, each a whole pass over its span), the feedback chain as `Stereo<FeedbackChain>`, the working
/// block, and the address log.
///
/// With a logger set, each tape access is reported. Clears, reads and native writes are reported at the access. A
/// resampled write is reported by probing: the samples around the write head are saved and zeroed, a probe sample
/// (kProbe, which no nonzero strength turns into 0 in both channels) is written with the frame's strengths, every
/// sample it left nonzero is reported, and the storage is restored before the frame's own write. The window widens
/// until the samples at its edges are untouched, so the whole triangle is reported at any rate. A strength of 0 adds
/// nothing in any order, so a sample written only at 0 is not reported. Without a logger nothing is reported.
///
/// Each order is a sibling over this base, never one derived from the other, so neither can be rendered as the other.
class DelayReferenceBase {
public:
	/// @brief The longest block either order renders, in frames: the engine's.
	static constexpr size_t kMaxBlockFrames = 128;

	/// @brief The feedback's stages, Q31: shaped by the amount and saturated, then high-passed.
	using FeedbackChain = Chain<FeedbackShaper, Pinned<ShiftTracker<11>, ShiftTracker<11>::Mode::REMOVE>>;

	/// @brief The high-pass's memories to 0 in both channels; the impulse response's history is kept.
	void resetHighPass() { feedback_.on<&FeedbackChain::reset_part<1>>(); }

	/// @brief Reports every tape access from the next block on; an empty logger reports none.
	void setLogger(AddressLogger logger) { log_ = std::move(logger); }

	/// @brief The feedback chain on both channels, between blocks.
	[[nodiscard]] const Stereo<FeedbackChain>& feedback() const { return feedback_; }

protected:
	DelayReferenceBase() = default;

	/// @brief The feedback chain's settings from @p settings.
	void configure(const DelayBlock& settings) {
		feedback_.configure(FeedbackChain::Settings{
		    FeedbackShaper::Settings{.feedback = settings.feedback,
		                             .analog = settings.analog,
		                             .drive = static_cast<uint32_t>(settings.analog_saturation)},
		    ShiftTracker<11>::Settings{},
		});
	}

	/// @brief The feedback chain on @p work, in place, both channels.
	void renderFeedback(std::span<StereoSample> work) { feedback_.renderBlock(work, work); }

	/// @return The working block, kMaxBlockFrames frames.
	[[nodiscard]] std::span<StereoSample> scratch() { return scratch_; }

	[[nodiscard]] static int32_t indexOf(const DelayTape& tape, const StereoSample* sample) {
		return static_cast<int32_t>(sample - tape.begin());
	}

	/// @return The write head's index: kWriteLag behind the read head, wrapped into the storage.
	[[nodiscard]] static int32_t writeHeadIndex(const DelayTape& tape) {
		auto index = static_cast<int32_t>(&tape.current() - tape.begin()) - static_cast<int32_t>(DelayTape::kWriteLag);
		if (index < 0) {
			index += static_cast<int32_t>(tape.sizeIncludingExtra());
		}
		return index;
	}

	void note(TapeRole tape, TapeAccessKind kind, int32_t frame, int32_t index) const {
		if (log_) {
			log_({.tape = tape, .kind = kind, .frame = frame, .index = index});
		}
	}

	/// @brief The probe a resampled write is reported by. Each product rounds toward minus infinity, so a positive
	/// strength leaves the left channel at -1 or below and a negative one the right; 2^20 keeps every sum of
	/// overlapping writes far inside q31.
	static constexpr StereoSample kProbe{.l = -(1 << 20), .r = 1 << 20};

	/// @brief Reports the samples @p tape's resampled write at @p strength1, @p strength2 touches, leaving the
	/// storage as it was.
	void probeResampledWrite(TapeRole role, int32_t frame, DelayTape& tape, int32_t strength1,
	                         int32_t strength2) const {
		if (!log_) {
			return;
		}
		const auto ring = static_cast<int32_t>(tape.sizeIncludingExtra());
		const int32_t head = writeHeadIndex(tape);
		std::vector<StereoSample> saved;
		std::vector<int32_t> changed;
		for (int32_t radius = probe_radius_;; radius *= 2) {
			const int32_t width = std::min((2 * radius) + 1, ring);
			const int32_t first = (((head - radius) % ring) + ring) % ring;
			auto at = [&](int32_t k) { return tape.begin() + ((first + k) % ring); };
			saved.resize(static_cast<size_t>(width));
			for (int32_t k = 0; k < width; ++k) {
				saved[static_cast<size_t>(k)] = *at(k);
				*at(k) = {.l = 0, .r = 0};
			}
			tape.writeResampled(kProbe, strength1, strength2);
			changed.clear();
			for (int32_t k = 0; k < width; ++k) {
				const StereoSample now = *at(k);
				if (now.l != 0 || now.r != 0) {
					changed.push_back(k);
				}
				*at(k) = saved[static_cast<size_t>(k)];
			}
			const bool whole = width == ring;
			const bool edge = !changed.empty() && (changed.front() == 0 || changed.back() == width - 1);
			if (whole || !edge) {
				probe_radius_ = std::max(kMinProbeRadius, radius / 2);
				for (const int32_t k : changed) {
					note(role, TapeAccessKind::WRITE, frame, indexOf(tape, at(k)));
				}
				return;
			}
		}
	}

	bool read(DelayTape& primary, std::span<StereoSample> work, int32_t first_frame) const {
		bool wrapped = false;
		int32_t frame = first_frame;
		if (primary.isNative()) {
			for (StereoSample& sample : work) {
				note(TapeRole::PRIMARY, TapeAccessKind::CLEAR, frame, indexOf(primary, &primary.current()));
				wrapped = primary.clearAndMoveOn() || wrapped;
				note(TapeRole::PRIMARY, TapeAccessKind::READ, frame, indexOf(primary, &primary.current()));
				sample = primary.current();
				++frame;
			}
			return wrapped;
		}

		for (StereoSample& sample : work) {
			// 16.16 strengths of the sample under the head and the next.
			const int32_t strength2 = primary.advance([&] {
				note(TapeRole::PRIMARY, TapeAccessKind::CLEAR, frame, indexOf(primary, &primary.current()));
				wrapped = primary.clearAndMoveOn() || wrapped;
			});
			const int32_t strength1 = 65536 - strength2;

			StereoSample* next = &primary.current() + 1;
			if (next == primary.end()) {
				next = primary.begin();
			}
			note(TapeRole::PRIMARY, TapeAccessKind::READ, frame, indexOf(primary, &primary.current()));
			note(TapeRole::PRIMARY, TapeAccessKind::READ, frame, indexOf(primary, next));
			const StereoSample from1 = primary.current();
			const StereoSample from2 = *next;

			sample.l =
			    (multiply_32x32_rshift32(from1.l, strength1 << 14) + multiply_32x32_rshift32(from2.l, strength2 << 14))
			    << 2;
			sample.r =
			    (multiply_32x32_rshift32(from1.r, strength1 << 14) + multiply_32x32_rshift32(from2.r, strength2 << 14))
			    << 2;
			++frame;
		}
		return wrapped;
	}

	static void mix(std::span<StereoSample> work, std::span<StereoSample> block, bool cross) {
		for (auto [feedback, output] : std::views::zip(work, block)) {
			const StereoSample delayed = feedback;
			if (cross) {
				feedback.l = delayed.r;
				feedback.r = ((output.l + output.r) >> 1) + delayed.l;
			}
			else {
				feedback += output;
			}
			output += delayed;
		}
	}

	void writePrimary(DelayTape& primary, DelayTape::Head head, std::span<const StereoSample> work,
	                  int32_t first_frame) const {
		int32_t frame = first_frame;
		if (primary.isNative()) {
			auto index = (head.pos - primary.begin()) - DelayTape::kWriteLag;
			if (index < 0) {
				index += static_cast<std::ptrdiff_t>(primary.sizeIncludingExtra());
			}
			StereoSample* write_pos = primary.begin() + index;
			for (const StereoSample sample : work) {
				note(TapeRole::PRIMARY, TapeAccessKind::WRITE, frame, indexOf(primary, write_pos));
				primary.writeNativeAndMoveOn(sample, write_pos);
				++frame;
			}
			return;
		}

		primary.setHead(head);
		for (const StereoSample sample : work) {
			const int32_t strength2 = primary.advance([&] { primary.moveOn(); });
			const int32_t strength1 = 65536 - strength2;
			probeResampledWrite(TapeRole::PRIMARY, frame, primary, strength1, strength2);
			primary.writeResampled(sample, strength1, strength2);
			++frame;
		}
	}

	DelayBlockResult writeSecondary(DelayTape& secondary, std::span<const StereoSample> work,
	                                int32_t first_frame) const {
		DelayBlockResult result{.wrapped = false, .secondary_written = 0};
		int32_t frame = first_frame;
		auto step = [&] {
			note(TapeRole::SECONDARY, TapeAccessKind::CLEAR, frame, indexOf(secondary, &secondary.current()));
			result.wrapped = secondary.clearAndMoveOn() || result.wrapped;
			++result.secondary_written;
		};

		if (secondary.isNative()) {
			for (const StereoSample sample : work) {
				step();
				note(TapeRole::SECONDARY, TapeAccessKind::WRITE, frame, writeHeadIndex(secondary));
				secondary.writeNative(sample);
				++frame;
			}
			return result;
		}

		for (const StereoSample sample : work) {
			const int32_t strength2 = secondary.advance(step);
			const int32_t strength1 = 65536 - strength2;
			probeResampledWrite(TapeRole::SECONDARY, frame, secondary, strength1, strength2);
			secondary.writeResampled(sample, strength1, strength2);
			++frame;
		}
		return result;
	}

private:
	/// @brief The feedback chain on both channels: the impulse response's history and the high-pass's memory per
	/// channel.
	Stereo<FeedbackChain> feedback_;
	/// @brief The working block: the delayed signal read, shaped and mixed, then written back. A block is at most
	/// kMaxBlockFrames frames.
	std::array<StereoSample, kMaxBlockFrames> scratch_{};
	/// @brief Reports each tape access; empty when nothing listens.
	AddressLogger log_;
	/// @brief The smallest probe window's half-width, in samples: wider than a write at up to 8x.
	static constexpr int32_t kMinProbeRadius = 32;
	/// @brief The half-width the next probe starts from: half the last one that held its write, so a run of writes
	/// at one rate rarely widens twice.
	mutable int32_t probe_radius_ = kMinProbeRadius;
};

/// @brief The staged order, test-local: the whole block read, its feedback shaped, mixed, then written back to both
/// tapes, with the feedback as a `Stereo<FeedbackChain>`: the record of the staged order.
///
/// The staged order reads the whole block before writing any of it back. When a block's walk laps the tape (on
/// 128-frame blocks, delays shorter than 129 samples: rates of 2130836488 and up, above 2^38/129), a later frame reads
/// or clears a sample before an earlier frame's write reaches it, and echoes are lost
/// (.superpowers/findings/delay-lap/).
/// @ref FrameOrderDelayProcessor is the delay line's order.
class ReferenceDelayProcessor : public DelayReferenceBase {
public:
	/// @brief One block, staged, with `DelayProcessor::process`'s signature.
	[[nodiscard]] DelayBlockResult process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
	                                       const DelayBlock& settings) {
		const bool reading = primary != nullptr && primary->isActive();
		const bool growing = secondary != nullptr && secondary->isActive();

		if (primary != nullptr) {
			primary->setupForRender(settings.rate);
		}
		if (growing) {
			secondary->setupForRender(settings.rate);
		}

		const std::span<StereoSample> work = scratch().first(block.size());

		DelayBlockResult result{.wrapped = false, .secondary_written = 0};
		DelayTape::Head head{};
		if (reading) {
			head = primary->head();
			result.wrapped = read(*primary, work, 0);
		}
		else {
			std::ranges::fill(work, StereoSample{.l = 0, .r = 0});
		}

		configure(settings);
		renderFeedback(work);
		mix(work, block, settings.ping_pong && settings.render_in_stereo);

		if (reading) {
			writePrimary(*primary, head, work, 0);
		}
		if (growing) {
			// The secondary tape's wraps replace the primary's: it is the tape that will play on.
			result = writeSecondary(*secondary, work, 0);
		}
		return result;
	}
};

/// @brief The delay line's order: each frame read, its feedback shaped, mixed, and written to both tapes before the
/// next frame is read. Every echo lands where the tape puts it, D = S − 1 samples on a native tape of S samples and
/// S / spin on a resampling one, however short the delay; the staged order (@ref ReferenceDelayProcessor) drops or
/// damages echoes when a block laps the tape and otherwise equals this bit for bit. The measurements are in
/// .superpowers/findings/delay-lap/. A fused delay kernel must equal this on every workload.
class FrameOrderDelayProcessor : public DelayReferenceBase {
public:
	/// @brief One block in frame order, with `DelayProcessor::process`'s signature: each frame read, shaped, mixed and
	/// written to both tapes before the next is read.
	[[nodiscard]] DelayBlockResult process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
	                                       const DelayBlock& settings) {
		const bool reading = primary != nullptr && primary->isActive();
		const bool growing = secondary != nullptr && secondary->isActive();

		if (primary != nullptr) {
			primary->setupForRender(settings.rate);
		}
		if (growing) {
			secondary->setupForRender(settings.rate);
		}

		configure(settings);
		const bool cross = settings.ping_pong && settings.render_in_stereo;
		bool primary_wrapped = false;
		DelayBlockResult grown{.wrapped = false, .secondary_written = 0};
		for (size_t i = 0; i < block.size(); ++i) {
			const auto frame = static_cast<int32_t>(i);
			const std::span<StereoSample> work = scratch().subspan(i, 1);
			const std::span<StereoSample> out = block.subspan(i, 1);
			DelayTape::Head head{};
			if (reading) {
				head = primary->head();
				primary_wrapped = read(*primary, work, frame) || primary_wrapped;
			}
			else {
				work.front() = {.l = 0, .r = 0};
			}
			renderFeedback(work);
			mix(work, out, cross);
			if (reading) {
				writePrimary(*primary, head, work, frame);
			}
			if (growing) {
				const DelayBlockResult step = writeSecondary(*secondary, work, frame);
				grown.wrapped = step.wrapped || grown.wrapped;
				grown.secondary_written += step.secondary_written;
			}
		}
		if (growing) {
			return grown;
		}
		return {.wrapped = primary_wrapped, .secondary_written = 0};
	}
};

} // namespace delay_reference
