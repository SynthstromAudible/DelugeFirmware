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

// Where the staged delay order is wrong. The staged block reads every frame, then writes every frame back; the
// delay line's order, which the fused kernel takes, walks each frame (read, shape, mix, write) before the next. The two
// can part only where a frame writes a tape sample that a later frame of the same block clears or reads: on 128-frame
// blocks, delays shorter than 129 samples, where the block's walk laps the tape and the staged order loses echoes
// (.superpowers/findings/delay-lap/). A lapping block parts them once the sample written there is nonzero; one whose
// lapped write is zero leaves them equal. The staged reference's address log names those lapping blocks on every
// recorded workload, and the two references are run side by side to show they part there and nowhere else.
// delay_echo_spec measures the frame order's echoes against where the tape puts them.

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
#include <optional>
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

/// The delay line's order.
using FrameOrder = delay_reference::FrameOrderDelayProcessor;

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
		DelayBlockResult process(std::span<StereoSample> block, DelayTape* p, DelayTape* s,
		                         const DelayBlock& settings) {
			primary = p != nullptr ? *p : DelayTape{};
			secondary = s != nullptr ? *s : DelayTape{};
			return ReferenceDelayProcessor::process(block, p, s, settings);
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

/// @return @p blocks (ascending, unique) as ranges: "2-11", or "2, 4-5".
std::string blockRanges(const std::vector<size_t>& blocks) {
	std::string out;
	for (size_t i = 0; i < blocks.size();) {
		size_t j = i;
		while (j + 1 < blocks.size() && blocks[j + 1] == blocks[j] + 1) {
			++j;
		}
		out += (out.empty() ? "" : ", ") + std::to_string(blocks[i]);
		if (j > i) {
			out += "-" + std::to_string(blocks[j]);
		}
		i = j + 1;
	}
	return out;
}

/// @return @p writes summarised per tape, one line each: the blocks they fall in, how many, the samples they span,
/// the frames that made them, and (out of order) how many a later frame cleared or read, and the latest such frame.
std::vector<std::string> summarise(std::string_view workload, const std::vector<FlaggedWrite>& writes) {
	using delay_reference::TapeAccessKind;
	using delay_reference::TapeRole;
	struct Group {
		std::vector<size_t> blocks;
		size_t writes = 0;
		int32_t low = std::numeric_limits<int32_t>::max();
		int32_t high = -1;
		int32_t first_write = std::numeric_limits<int32_t>::max();
		int32_t last_write = -1;
		size_t cleared = 0;
		size_t read = 0;
		int32_t later = -1;
	};
	std::map<TapeRole, Group> groups;
	for (const FlaggedWrite& w : writes) {
		Group& g = groups[w.tape];
		if (g.blocks.empty() || g.blocks.back() != w.block) {
			g.blocks.push_back(w.block);
		}
		++g.writes;
		g.low = std::min(g.low, w.index);
		g.high = std::max(g.high, w.index);
		g.first_write = std::min(g.first_write, w.write_frame);
		g.last_write = std::max(g.last_write, w.write_frame);
		g.cleared += w.later == TapeAccessKind::CLEAR ? 1 : 0;
		g.read += w.later == TapeAccessKind::READ ? 1 : 0;
		g.later = std::max(g.later, w.later_frame);
	}
	std::vector<std::string> out;
	for (const auto& [tape, g] : groups) {
		std::string line = std::string{workload} + (tape == TapeRole::PRIMARY ? " primary" : " secondary") + " blocks "
		                   + blockRanges(g.blocks) + ": " + std::to_string(g.writes) + " writes in ["
		                   + std::to_string(g.low) + ", " + std::to_string(g.high) + "] by frames "
		                   + std::to_string(g.first_write) + "-" + std::to_string(g.last_write);
		if (g.later >= 0) {
			line += ", " + std::to_string(g.cleared) + " cleared and " + std::to_string(g.read)
			        + " read by frames up to " + std::to_string(g.later);
		}
		out.push_back(line);
	}
	return out;
}

/// @return The first block of @p check whose primary laps: a frame writes a sample a later frame clears or reads.
std::optional<size_t> firstLappingBlock(const OrderCheck& check) {
	std::optional<size_t> first;
	for (const FlaggedWrite& w : check.out_of_order) {
		if (w.tape == delay_reference::TapeRole::PRIMARY && (!first || w.block < *first)) {
			first = w.block;
		}
	}
	return first;
}

/// Each workload whose two orders part, as "<name>: laps from block <a>, parts at block <b>". A lapping block parts
/// the orders once frame 0 writes a nonzero sample that frame 127 reads. On the lap workloads that is block 2 when the
/// impulse falls on frame 0, since frame 0 writes the impulse itself. Otherwise it is block 4: the impulse's first
/// echo, in block 3, charges the feedback high-pass's low-pass, so block 4's frame 0 writes the high-pass's small
/// nonzero residual onto the lapped sample.
const std::vector<std::string> kParted{
    "highest_rate: laps from block 2, parts at block 2",       "spin_689: laps from block 4, parts at block 4",
    "spin_689_secondary: laps from block 8, parts at block 8", "rate_above_2_31: laps from block 2, parts at block 2",
    "lap_f0_fb075: laps from block 2, parts at block 2",       "lap_f5_fb075: laps from block 2, parts at block 4",
    "lap_f127_fb075: laps from block 2, parts at block 4",     "lap_f0_fb099: laps from block 2, parts at block 2",
    "lap_f5_fb099: laps from block 2, parts at block 4",       "lap_f127_fb099: laps from block 2, parts at block 4",
    "lap_threshold: laps from block 2, parts at block 4",
};

/// The staged order's lapping blocks, per workload and tape: every write a later frame of its block clears or reads.
/// - The neutral tape stepped to the highest rate walks 128 steps per sample, its whole ring in a block, so the last
///   frame clears the left tails of the first frames' sped-up triangles (blocks 2-3 of highest_rate).
/// - A 128-sample tape at a rate of 2130836488 or more plays a delay of 127 samples (native) or about 128 (resampling
///   just below 1x, its native made 2^31): a 128-frame block walks 127 or 128 steps of its 148-sample ring, and the
///   last frame reads the sample the first frame writes.
/// - A rate above 2^31 has a 113-sample tape step 196 times per sample, lapping it every frame; the secondary's loop
///   is already in frame order, so its flagged writes part nothing.
const std::vector<std::string> kOutOfOrder{
    "highest_rate primary blocks 2-11: 268 writes in [8, 256] by frames 0-1, 254 cleared and 14 read by frames up to "
    "127",
    "spin_689 primary blocks 4-9: 6 writes in [8, 108] by frames 0-0, 0 cleared and 6 read by frames up to 127",
    "spin_689_secondary primary blocks 8-11: 4 writes in [48, 108] by frames 0-0, 0 cleared and 4 read by frames up to "
    "127",
    "rate_above_2_31 primary blocks 2-11: 156501 writes in [0, 2305] by frames 0-126, 156497 cleared and 4 read by "
    "frames up to 127",
    "rate_above_2_31 secondary blocks 2: 16891 writes in [0, 132] by frames 0-126, 16891 cleared and 0 read by frames "
    "up to 127",
    "lap_f0_fb075 primary blocks 2-10: 9 writes in [8, 136] by frames 0-0, 0 cleared and 9 read by frames up to 127",
    "lap_f5_fb075 primary blocks 2-10: 9 writes in [8, 136] by frames 0-0, 0 cleared and 9 read by frames up to 127",
    "lap_f127_fb075 primary blocks 2-11: 10 writes in [8, 136] by frames 0-0, 0 cleared and 10 read by frames up to "
    "127",
    "lap_f0_fb099 primary blocks 2-10: 9 writes in [8, 136] by frames 0-0, 0 cleared and 9 read by frames up to 127",
    "lap_f5_fb099 primary blocks 2-10: 9 writes in [8, 136] by frames 0-0, 0 cleared and 9 read by frames up to 127",
    "lap_f127_fb099 primary blocks 2-11: 10 writes in [8, 136] by frames 0-0, 0 cleared and 10 read by frames up to "
    "127",
    "lap_threshold primary blocks 2-10: 9 writes in [8, 136] by frames 0-0, 0 cleared and 9 read by frames up to 127",
};

/// Every write to a sample no clear has reached since its tape was made: a fresh secondary's first resampled write,
/// whose triangle reaches back past the storage's start into its end. The head clears those samples before the tape
/// is swapped in and read, so nothing plays them, in either order.
const std::vector<std::string> kUncleared{
    "growth_swap secondary blocks 24: 2 writes in [6162, 6163] by frames 0-0",
    "growth_moving secondary blocks 10: 2 writes in [2578, 2579] by frames 0-0",
    "spin_689_secondary secondary blocks 2: 174 writes in [88046, 88219] by frames 0-0",
};

} // namespace

// clang-format off
describe delay_frame_order("Delay frame order", ${
	it("parts from the staged order only where a block laps its tape, on the workloads that reach a delay shorter "
	   "than a 128-frame block", _ {
		std::vector<std::string> parted;
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const OrderCheck& check = orderChecks().at(static_cast<size_t>(workload.id));
			expect(check.writes).to_be_greater_than(size_t{0});
			const std::optional<size_t> lap = firstLappingBlock(check);
			const auto part = delay_workloads::first_parting<Staged, FrameOrder>(workload);
			if (!lap && !part) {
				continue;
			}
			parted.push_back(std::string{workload.name} + ": laps from block "
			                 + (lap ? std::to_string(*lap) : std::string{"none"}) + ", parts at block "
			                 + (part ? std::to_string(part->block) : std::string{"none"}));
		}
		expect(parted).to_equal(kParted);
	});

	it("records the same as the staged order on every workload but those, and differs on each of them", _ {
		std::vector<std::string> differ;
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const auto i = static_cast<size_t>(workload.id);
			if (delay_workloads::kRecorded.at(i) != delay_workloads::kRecordedFrameOrder.at(i)) {
				differ.emplace_back(workload.name);
			}
		}
		std::vector<std::string> expected;
		for (const std::string& line : kParted) {
			expected.push_back(line.substr(0, line.find(':')));
		}
		expect(differ).to_equal(expected);
	});

	it("names the blocks where the staged order laps the tape, and the fresh secondaries' writes past their start", _ {
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
