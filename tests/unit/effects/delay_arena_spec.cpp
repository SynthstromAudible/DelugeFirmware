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

// effects::DelayArena over a test memory: regions lent from the test's own allocations, each a first-fit heap, with
// counts of the lends, returns and maintenance requests. The arena lends when its margin is short, takes tapes from
// its regions, returns the regions it no longer needs, and counts a take that finds no room.

#include "effects/delay_arena.h"

#include "mock_panic.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

using deluge::dsp::StereoSample;
using deluge::effects::ArenaChange;
using deluge::effects::DelayArena;
using deluge::effects::DelayArenaMemory;

namespace {

/// A first-fit heap over one region: its blocks as (offset, size), sorted by offset, 16-byte aligned.
struct TestHeap {
	std::byte* base = nullptr;
	std::size_t size = 0;
	std::vector<std::pair<std::size_t, std::size_t>> blocks;

	void* alloc(std::size_t bytes) {
		const std::size_t need = (bytes + 15) & ~std::size_t{15};
		std::size_t at = 0;
		for (auto next = blocks.begin();; ++next) {
			const std::size_t end = next == blocks.end() ? size : next->first;
			if (end >= at && end - at >= need) {
				blocks.insert(next, {at, need});
				return base + at;
			}
			if (next == blocks.end()) {
				return nullptr;
			}
			at = next->first + next->second;
		}
	}
	void free(void* block) {
		const auto offset = static_cast<std::size_t>(static_cast<std::byte*>(block) - base);
		std::erase_if(blocks, [&](const auto& b) { return b.first == offset; });
	}
};

/// The test memory: what the arena was lent and asked.
struct TestMemory {
	std::vector<std::unique_ptr<std::byte[]>> lent;
	std::vector<std::unique_ptr<TestHeap>> heaps;
	int lends = 0;
	int unlends = 0;
	int maintenance = 0;
	bool fail_next_lend = false;
};

TestMemory g_test_memory;
TestMemory* g_memory = &g_test_memory;

/// @return The test memory, emptied for a new case.
TestMemory& fresh_memory() {
	g_test_memory = TestMemory{};
	return g_test_memory;
}

void* lend(std::size_t bytes) {
	if (g_memory->fail_next_lend) {
		g_memory->fail_next_lend = false;
		return nullptr;
	}
	g_memory->lends++;
	g_memory->lent.push_back(std::unique_ptr<std::byte[]>(new (std::align_val_t{16}) std::byte[bytes]));
	return g_memory->lent.back().get();
}
void unlend(void* region) {
	g_memory->unlends++;
	std::erase_if(g_memory->lent, [&](const auto& r) { return r.get() == region; });
}
void* create_heap(void* base, std::size_t bytes) {
	g_memory->heaps.push_back(std::make_unique<TestHeap>(TestHeap{static_cast<std::byte*>(base), bytes, {}}));
	return g_memory->heaps.back().get();
}
void* heap_alloc(void* heap, std::size_t bytes) {
	return static_cast<TestHeap*>(heap)->alloc(bytes);
}
void heap_free(void* heap, void* block) {
	static_cast<TestHeap*>(heap)->free(block);
}
void request_maintenance() {
	g_memory->maintenance++;
}

constexpr DelayArenaMemory kMemory{lend, unlend, create_heap, heap_alloc, heap_free, request_maintenance};

/// The largest tape, in frames.
constexpr std::size_t kMaxFrames = DelayArena::kMaxTapeBytes / sizeof(StereoSample);

/// @brief Runs prepare, commit and finish with @p on_delta, as an owner's projection or the arena's job does.
void maintain(DelayArena& arena, int on_delta) {
	ArenaChange change = arena.prepare(on_delta);
	arena.commit(change);
	arena.finish(change);
}

/// @brief Maintains @p arena until it lends nothing more, as its job does on each wake.
void settle(DelayArena& arena) {
	for (int i = 0; i < 8; i++) {
		maintain(arena, 0);
	}
}

/// A reserve of 68,750 frames (550,000 bytes): a region holds two, not three.
constexpr std::size_t kTwoPerRegionFrames = 68750;

} // namespace

// clang-format off
describe delay_arena("the delay's arena", ${
	it("lends a region when its first owner is counted on, and holds the margin", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		expect(arena.regions()).to_equal(std::size_t{0});
		maintain(arena, +1);
		expect(arena.regions()).to_equal(std::size_t{1});
		expect(arena.owners_on()).to_equal(1);
		expect(arena.holds_margin()).to_be_true();
		expect(memory.unlends).to_equal(0);
		maintain(arena, -1);
	});
	it("takes tapes until a region is full, counts the take that finds no room, and asks for maintenance", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +1);
		StereoSample* a = arena.take(kMaxFrames);
		StereoSample* b = arena.take(kMaxFrames);
		expect(a).not_().to_be_null();
		expect(b).not_().to_be_null();
		expect(arena.take(kMaxFrames)).to_be_null();
		expect(arena.shortfalls()).to_equal(uint32_t{1});
		expect(memory.maintenance).to_equal(3);
		expect(arena.live_tapes()).to_equal(std::size_t{2});
		arena.give_back(a, kMaxFrames);
		arena.give_back(b, kMaxFrames);
		maintain(arena, -1);
	});
	it("lends another region once its tapes have used the margin", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +1);
		StereoSample* a = arena.take(kMaxFrames);
		StereoSample* b = arena.take(kMaxFrames);
		maintain(arena, 0);
		expect(arena.regions()).to_equal(std::size_t{2});
		expect(arena.holds_margin()).to_be_true();
		StereoSample* c = arena.take(kMaxFrames);
		expect(c).not_().to_be_null();
		expect(arena.live_tapes()).to_equal(std::size_t{3});
		for (StereoSample* tape : {a, b, c}) {
			arena.give_back(tape, kMaxFrames);
		}
		maintain(arena, -1);
	});
	it("keeps a region its tapes left empty while its owners stay on", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +1);
		StereoSample* a = arena.take(kMaxFrames);
		StereoSample* b = arena.take(kMaxFrames);
		maintain(arena, 0);
		arena.give_back(a, kMaxFrames);
		arena.give_back(b, kMaxFrames);
		maintain(arena, 0);
		expect(arena.regions()).to_equal(std::size_t{2});
		expect(memory.unlends).to_equal(0);
		maintain(arena, -1);
	});
	it("lends nothing for a take that leaves the margin held", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +2);
		maintain(arena, 0);
		expect(arena.regions()).to_equal(std::size_t{2});
		const int lends = memory.lends;
		StereoSample* tape = arena.take(16);
		maintain(arena, 0);
		expect(memory.lends).to_equal(lends);
		arena.give_back(tape, 16);
		maintain(arena, -2);
	});
	it("returns the regions an owner counted out leaves spare", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +12);
		maintain(arena, 0);
		expect(arena.regions()).to_equal(std::size_t{2});
		maintain(arena, -11);
		expect(arena.regions()).to_equal(std::size_t{1});
		expect(memory.unlends).to_equal(1);
		maintain(arena, -1);
	});
	it("lends nothing to count an owner out", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +1);
		StereoSample* tape = arena.take(kMaxFrames);
		const int lends = memory.lends;
		arena.give_back(tape, kMaxFrames);
		maintain(arena, -1);
		expect(memory.lends).to_equal(lends);
		expect(arena.regions()).to_equal(std::size_t{0});
	});
	it("returns every region once no owner is on", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +1);
		maintain(arena, -1);
		expect(arena.regions()).to_equal(std::size_t{0});
		expect(memory.unlends).to_equal(1);
		expect(memory.lent.size()).to_equal(std::size_t{0});
	});
	it("holds free bytes for a neutral-rate tape per owner on", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +12);
		expect(arena.regions()).to_equal(std::size_t{1});
		expect(arena.holds_margin()).to_be_false();
		maintain(arena, 0);
		expect(arena.regions()).to_equal(std::size_t{2});
		expect(arena.holds_margin()).to_be_true();
		maintain(arena, -12);
	});
	it("keeps counting an owner whose region could not be lent, and lends at the next maintenance", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		memory.fail_next_lend = true;
		maintain(arena, +1);
		expect(arena.regions()).to_equal(std::size_t{0});
		expect(arena.owners_on()).to_equal(1);
		expect(arena.take(16)).to_be_null();
		maintain(arena, 0);
		expect(arena.regions()).to_equal(std::size_t{1});
		maintain(arena, -1);
	});
	it("lends nothing while the margin holds", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +1);
		const int lends = memory.lends;
		maintain(arena, 0);
		maintain(arena, +1);
		expect(memory.lends).to_equal(lends);
		maintain(arena, -2);
	});
	it("places five owners' largest reserves before any of their tapes is taken", _ {
		fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +5);
		std::array<std::size_t, 5> slots{};
		for (std::size_t& slot : slots) {
			slot = arena.open_slot(kMaxFrames);
		}
		settle(arena);
		expect(arena.holds_margin()).to_be_true();
		std::array<StereoSample*, 5> tapes{};
		for (StereoSample*& tape : tapes) {
			tape = arena.take(kMaxFrames);
		}
		expect(arena.shortfalls()).to_equal(uint32_t{0});
		for (StereoSample* tape : tapes) {
			arena.give_back(tape, kMaxFrames);
		}
		for (std::size_t slot : slots) {
			arena.close_slot(slot);
		}
		maintain(arena, -5);
	});
	it("packs reserves into regions where a byte count would find room that is not there", _ {
		fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +3);
		std::array<std::size_t, 3> slots{};
		for (std::size_t& slot : slots) {
			slot = arena.open_slot(kTwoPerRegionFrames);
		}
		settle(arena);
		expect(arena.regions()).to_equal(std::size_t{3});
		for (std::size_t slot : slots) {
			arena.close_slot(slot);
		}
		maintain(arena, -3);
	});
	it("asks for maintenance when the render raises a reserve past the margin", _ {
		TestMemory& memory = fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +1);
		const std::size_t slot = arena.open_slot(DelayArena::kNeutralTapeBytes / sizeof(StereoSample));
		settle(arena);
		const int before = memory.maintenance;
		arena.raise_reserve(slot, kMaxFrames);
		expect(memory.maintenance).to_be_greater_than(before);
		expect(arena.holds_margin()).to_be_false();
		settle(arena);
		expect(arena.holds_margin()).to_be_true();
		arena.close_slot(slot);
		maintain(arena, -1);
	});
	it("reuses a closed slot", _ {
		fresh_memory();
		DelayArena arena{kMemory};
		maintain(arena, +2);
		const std::size_t first = arena.open_slot(16);
		const std::size_t second = arena.open_slot(16);
		arena.close_slot(first);
		expect(arena.open_slot(16)).to_equal(first);
		arena.close_slot(first);
		arena.close_slot(second);
		maintain(arena, -2);
	});
	it("freezes on counting out an owner it never counted in", _ {
		fresh_memory();
		deluge::test::clear_freeze();
		DelayArena arena{kMemory};
		maintain(arena, -1);
		expect(deluge::test::last_freeze()).to_equal(std::string("DA01"));
		expect(arena.owners_on()).to_equal(0);
	});
	it("freezes on giving back storage it never handed out", _ {
		fresh_memory();
		deluge::test::clear_freeze();
		DelayArena arena{kMemory};
		maintain(arena, +1);
		StereoSample foreign[4]{};
		arena.give_back(foreign, 4);
		expect(deluge::test::last_freeze()).to_equal(std::string("DA02"));
		maintain(arena, -1);
	});
});
// clang-format on

CPPSPEC_SPEC(delay_arena)
