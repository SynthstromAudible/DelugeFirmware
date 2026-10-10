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

// The delay's frame order: whether one walk per frame (read, shape, mix, write) leaves the tapes as the staged block
// does (read every frame, then write every frame back). It does exactly when no frame writes a tape sample that a
// later frame of the same block clears or reads. The staged reference (reference_delay_processor.h) logs every clear,
// read and write address of every recorded workload (delay_workloads.h); the check below finds every write a later
// frame clears or reads, and the frame-by-frame order is run beside the staged one to show the two part exactly there.

#include "delay_workloads.h"
#include "reference_delay_processor.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayBlock;
using deluge::dsp::fixed::DelayBlockResult;
using deluge::dsp::fixed::DelayTape;

namespace {

/// The staged reference.
using Staged = delay_reference::ReferenceDelayProcessor;

/// The staged reference's steps in frame order: each frame read, shaped, mixed and written before the next.
struct FrameByFrame : delay_reference::ReferenceDelayProcessor {
	DelayBlockResult process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
	                         const DelayBlock& settings, std::span<StereoSample> scratch) {
		return processFrameByFrame(block, primary, secondary, settings, scratch);
	}
};

/// One sample a frame writes, flagged by the frame-order check.
struct FlaggedWrite {
	size_t block;
	delay_reference::TapeRole tape;
	int32_t index;       ///< The sample, in the tape's storage.
	int32_t write_frame; ///< The frame that writes it.
	delay_reference::TapeAccessKind later = delay_reference::TapeAccessKind::WRITE; ///< What a later frame does to it.
	int32_t later_frame = -1; ///< The last frame of the block that does it.
};

/// What the frame-order check found on a workload.
struct OrderCheck {
	size_t writes = 0;                      ///< Tape writes, one per sample a write touches, over every block.
	std::vector<FlaggedWrite> out_of_order; ///< Writes to a sample a later frame of the block clears or reads.
	std::vector<FlaggedWrite> uncleared;    ///< Writes to a sample no clear had reached since its tape was made.
};

/// Runs @p workload on the staged reference with every tape access logged, and checks, per block and per tape, that
/// each sample a frame writes is neither cleared nor read by a later frame of the block: it lies behind the read head
/// once that frame's read is done, or was cleared in an earlier block. Clears are the read head's clearAndMoveOn, one
/// per step (several per frame at a high rate); reads are the sample under the head and, resampling, the next; writes
/// are a native write's sample or every sample a resampled write's triangle touches (four from two past the write head
/// slowed down, up to sixteen past it and twice the spin behind it sped up). Separately, it flags each write to a
/// sample no clear has reached since its tape was made: clear() zeroes a fresh tape's first kWriteLag + 2 samples, and
/// a primary the workload starts from holds a lap it has played, so counts as cleared throughout.
OrderCheck checkFrameOrder(const delay_workloads::Workload& workload) {
	using delay_reference::TapeAccess;
	using delay_reference::TapeAccessKind;
	using delay_reference::TapeRole;
	// The staged reference, keeping the tapes each block is handed as they were before it.
	struct Logged : delay_reference::ReferenceDelayProcessor {
		DelayTape primary;
		DelayTape secondary;
		DelayBlockResult process(std::span<StereoSample> block, DelayTape* p, DelayTape* s, const DelayBlock& settings,
		                         std::span<StereoSample> scratch) {
			primary = p != nullptr ? *p : DelayTape{};
			secondary = s != nullptr ? *s : DelayTape{};
			return ReferenceDelayProcessor::process(block, p, s, settings, scratch);
		}
	};
	delay_workloads::WorkloadRun<Logged> run{workload};
	std::vector<TapeAccess> accesses;
	run.processor().setLogger([&](const TapeAccess& access) { accesses.push_back(access); });

	// Per tape storage: whether a clear (or the workload's starting lap) has reached each sample.
	std::map<const StereoSample*, std::vector<bool>> cleared;
	auto clearedOf = [&](const DelayTape& tape) -> std::vector<bool>& {
		auto [entry, made] = cleared.try_emplace(tape.begin(), tape.sizeIncludingExtra(), false);
		if (made) {
			std::fill_n(entry->second.begin(), DelayTape::kWriteLag + 2, true);
		}
		return entry->second;
	};
	if (run.primary().isActive()) {
		cleared.emplace(run.primary().begin(), std::vector<bool>(run.primary().sizeIncludingExtra(), true));
	}

	OrderCheck out;
	std::array<StereoSample, delay_workloads::kMaxFrames> block{};
	for (size_t b = 0; b < workload.blocks; ++b) {
		accesses.clear();
		const DelayTape* before = run.primary().isActive() ? &run.primary() : nullptr;
		const StereoSample* replaced = before != nullptr ? before->begin() : nullptr;
		std::ignore = run.block(b, block);

		for (const TapeRole role : {TapeRole::PRIMARY, TapeRole::SECONDARY}) {
			const DelayTape& tape = role == TapeRole::PRIMARY ? run.processor().primary : run.processor().secondary;
			if (!tape.isActive()) {
				continue;
			}
			const size_t ring = tape.sizeIncludingExtra();
			std::vector<int32_t> last_clear(ring, -1);
			std::vector<int32_t> last_read(ring, -1);
			std::vector<int32_t> first_clear(ring, std::numeric_limits<int32_t>::max());
			for (const TapeAccess& access : accesses) {
				if (access.tape != role) {
					continue;
				}
				const auto i = static_cast<size_t>(access.index);
				if (access.kind == TapeAccessKind::CLEAR) {
					last_clear[i] = std::max(last_clear[i], access.frame);
					first_clear[i] = std::min(first_clear[i], access.frame);
				}
				else if (access.kind == TapeAccessKind::READ) {
					last_read[i] = std::max(last_read[i], access.frame);
				}
			}
			std::vector<bool>& was_cleared = clearedOf(tape);
			for (const TapeAccess& access : accesses) {
				if (access.tape != role || access.kind != TapeAccessKind::WRITE) {
					continue;
				}
				const auto i = static_cast<size_t>(access.index);
				const FlaggedWrite write{.block = b, .tape = role, .index = access.index, .write_frame = access.frame};
				++out.writes;
				if (!was_cleared[i] && first_clear[i] > access.frame) {
					out.uncleared.push_back(write);
				}
				if (last_clear[i] > access.frame) {
					FlaggedWrite flagged = write;
					flagged.later = TapeAccessKind::CLEAR;
					flagged.later_frame = last_clear[i];
					out.out_of_order.push_back(flagged);
				}
				else if (last_read[i] > access.frame) {
					FlaggedWrite flagged = write;
					flagged.later = TapeAccessKind::READ;
					flagged.later_frame = last_read[i];
					out.out_of_order.push_back(flagged);
				}
			}
			for (size_t i = 0; i < ring; ++i) {
				if (last_clear[i] >= 0) {
					was_cleared[i] = true;
				}
			}
		}
		// A swap frees the old primary's storage, which a later tape may be given.
		if (run.swapped() && replaced != nullptr) {
			cleared.erase(replaced);
		}
	}
	return out;
}

/// @return checkFrameOrder on every workload, in WorkloadId order: run once, shared by the cases that read it.
const std::vector<OrderCheck>& orderChecks() {
	static const std::vector<OrderCheck> checks = [] {
		std::vector<OrderCheck> out;
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			out.push_back(checkFrameOrder(workload));
		}
		return out;
	}();
	return checks;
}

/// @return @p writes summarised per block and tape, one line each: how many, the samples they span, the frames that
/// made them, and (out of order) how many a later frame cleared or read, and the latest such frame.
std::vector<std::string> summarise(std::string_view workload, const std::vector<FlaggedWrite>& writes) {
	using delay_reference::TapeAccessKind;
	using delay_reference::TapeRole;
	struct Group {
		size_t samples = 0;
		int32_t low = std::numeric_limits<int32_t>::max();
		int32_t high = -1;
		int32_t first_write = std::numeric_limits<int32_t>::max();
		int32_t last_write = -1;
		size_t cleared = 0;
		size_t read = 0;
		int32_t later = -1;
	};
	std::map<std::pair<size_t, TapeRole>, Group> groups;
	for (const FlaggedWrite& w : writes) {
		Group& g = groups[{w.block, w.tape}];
		++g.samples;
		g.low = std::min(g.low, w.index);
		g.high = std::max(g.high, w.index);
		g.first_write = std::min(g.first_write, w.write_frame);
		g.last_write = std::max(g.last_write, w.write_frame);
		g.cleared += w.later == TapeAccessKind::CLEAR ? 1 : 0;
		g.read += w.later == TapeAccessKind::READ ? 1 : 0;
		g.later = std::max(g.later, w.later_frame);
	}
	std::vector<std::string> out;
	for (const auto& [key, g] : groups) {
		std::string line = std::string{workload} + " block " + std::to_string(key.first)
		                   + (key.second == TapeRole::PRIMARY ? " primary: " : " secondary: ")
		                   + std::to_string(g.samples) + " writes in [" + std::to_string(g.low) + ", "
		                   + std::to_string(g.high) + "] written by frames " + std::to_string(g.first_write) + "-"
		                   + std::to_string(g.last_write);
		if (g.later >= 0) {
			line += ", " + std::to_string(g.cleared) + " cleared and " + std::to_string(g.read)
			        + " read by frames up to " + std::to_string(g.later);
		}
		out.push_back(line);
	}
	return out;
}

/// Every write the frame-order check flags as out of order, summarised per block and tape: all at the top of the rate
/// range. The neutral tape stepped to the highest rate (128x, before its 128-sample secondary swaps in) walks its
/// whole ring in a block, so the last frame clears the left tails of the first frames' sped-up triangles. The
/// 128-sample tape at the highest rate (native at 2^31, so it resamples just below 1x) walks 127 steps in a block, so
/// the last frame reads, as the sample after the head, the one the first frame's slowed-down write reached two past
/// the write head. A rate above 2^31 has a 113-sample tape step 196 times per sample, lapping it every frame.
const std::vector<std::string> kOutOfOrder{
    "highest_rate block 2 primary: 130 writes in [128, 256] written by frames 0-1, 127 cleared and 3 read by frames "
    "up to 127",
    "highest_rate block 3 primary: 130 writes in [108, 236] written by frames 0-1, 127 cleared and 3 read by frames "
    "up to 127",
    "highest_rate block 4 primary: 1 writes in [108, 108] written by frames 0-0, 0 cleared and 1 read by frames up to "
    "127",
    "highest_rate block 5 primary: 1 writes in [88, 88] written by frames 0-0, 0 cleared and 1 read by frames up to "
    "127",
    "highest_rate block 6 primary: 1 writes in [68, 68] written by frames 0-0, 0 cleared and 1 read by frames up to "
    "127",
    "highest_rate block 7 primary: 1 writes in [48, 48] written by frames 0-0, 0 cleared and 1 read by frames up to "
    "127",
    "highest_rate block 8 primary: 1 writes in [28, 28] written by frames 0-0, 0 cleared and 1 read by frames up to "
    "127",
    "highest_rate block 9 primary: 1 writes in [8, 8] written by frames 0-0, 0 cleared and 1 read by frames up to 127",
    "highest_rate block 10 primary: 1 writes in [136, 136] written by frames 0-0, 0 cleared and 1 read by frames up to "
    "127",
    "highest_rate block 11 primary: 1 writes in [116, 116] written by frames 0-0, 0 cleared and 1 read by frames up to "
    "127",
    "spin_689 block 4 primary: 1 writes in [108, 108] written by frames 0-0, 0 cleared and 1 read by frames up to 127",
    "spin_689 block 5 primary: 1 writes in [88, 88] written by frames 0-0, 0 cleared and 1 read by frames up to 127",
    "spin_689 block 6 primary: 1 writes in [68, 68] written by frames 0-0, 0 cleared and 1 read by frames up to 127",
    "spin_689 block 7 primary: 1 writes in [48, 48] written by frames 0-0, 0 cleared and 1 read by frames up to 127",
    "spin_689 block 8 primary: 1 writes in [28, 28] written by frames 0-0, 0 cleared and 1 read by frames up to 127",
    "spin_689 block 9 primary: 1 writes in [8, 8] written by frames 0-0, 0 cleared and 1 read by frames up to 127",
    "spin_689_secondary block 8 primary: 1 writes in [108, 108] written by frames 0-0, 0 cleared and 1 read by frames "
    "up to 127",
    "spin_689_secondary block 9 primary: 1 writes in [88, 88] written by frames 0-0, 0 cleared and 1 read by frames up "
    "to 127",
    "spin_689_secondary block 10 primary: 1 writes in [68, 68] written by frames 0-0, 0 cleared and 1 read by frames "
    "up "
    "to 127",
    "spin_689_secondary block 11 primary: 1 writes in [48, 48] written by frames 0-0, 0 cleared and 1 read by frames "
    "up "
    "to 127",
    "rate_above_2_31 block 2 primary: 4482 writes in [112, 2305] written by frames 0-15, 4478 cleared and 4 read by "
    "frames up to 127",
    "rate_above_2_31 block 2 secondary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 3 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 4 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 5 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 6 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 7 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 8 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 9 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 10 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
    "rate_above_2_31 block 11 primary: 16891 writes in [0, 132] written by frames 0-126, 16891 cleared and 0 read by "
    "frames up to 127",
};

/// Every write to a sample no clear has reached since its tape was made: a fresh secondary's first resampled write,
/// whose triangle reaches back past the storage's start into its end. The head clears those samples before the tape
/// is swapped in and read, so nothing plays them.
const std::vector<std::string> kUncleared{
    "growth_swap block 24 secondary: 2 writes in [6162, 6163] written by frames 0-0",
    "growth_moving block 10 secondary: 2 writes in [2578, 2579] written by frames 0-0",
    "spin_689_secondary block 2 secondary: 174 writes in [88046, 88219] written by frames 0-0",
};

} // namespace

// clang-format off
describe delay_frame_order("Delay frame order", ${
	it("walks every workload frame by frame as it walks it staged, tapes and all, exactly where no frame's write lands "
	   "on a primary sample a later frame of its block clears or reads", _ {
		std::vector<std::string> disagree;
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const OrderCheck& check = orderChecks().at(static_cast<size_t>(workload.id));
			expect(check.writes).to_be_greater_than(size_t{0});
			const bool primary_in_order = std::ranges::none_of(check.out_of_order, [](const FlaggedWrite& w) {
				return w.tape == delay_reference::TapeRole::PRIMARY;
			});
			if (delay_workloads::first_parting<Staged, FrameByFrame>(workload).empty() != primary_in_order) {
				disagree.emplace_back(workload.name);
			}
		}
		expect(disagree).to_equal(std::vector<std::string>{});
	});

	it("writes no tape sample a later frame of its block clears or reads, on every workload but at the top of the rate "
	   "range", _ {
		std::vector<std::string> out_of_order;
		std::vector<std::string> uncleared;
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const OrderCheck& check = orderChecks().at(static_cast<size_t>(workload.id));
			std::ranges::copy(summarise(workload.name, check.out_of_order), std::back_inserter(out_of_order));
			std::ranges::copy(summarise(workload.name, check.uncleared), std::back_inserter(uncleared));
		}
		expect(out_of_order).to_equal(kOutOfOrder);
		expect(uncleared).to_equal(kUncleared);
	});
});
// clang-format on

CPPSPEC_MAIN(delay_frame_order);
