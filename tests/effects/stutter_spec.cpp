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

// effects::Stutter against the stutter reference (reference_stutter.h) over every workload (stutter_workloads.h),
// begun and ended as its owner hands the tape over, with a counting allocator over two static blocks: each workload as
// recorded; a failed allocation; a second begin while one plays; and each tape freed once.

#include "effects/stutter.h"

#include "dsp-next/core/stereo_sample.h"
#include "stutter_workload_records.h"
#include "stutter_workloads.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <tuple>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

namespace sw = stutter_workloads;

constexpr std::size_t kTapeBytes = 720000; ///< Room for the largest tape: 88,220 frames of 8 bytes.

/// The allocator's blocks, and what has been asked of it.
struct TestHeap {
	alignas(16) std::array<std::byte, kTapeBytes> tapes[2]{};
	bool in_use[2]{};
	int allocations = 0;
	int releases = 0;
	int double_releases = 0;
	bool fail = false; ///< Fails every request.

	/// @return The blocks handed out and not yet released.
	[[nodiscard]] int live() const { return (in_use[0] ? 1 : 0) + (in_use[1] ? 1 : 0); }

	void reset() {
		in_use[0] = in_use[1] = false;
		allocations = releases = double_releases = 0;
		fail = false;
		std::memset(tapes, 0xA5, sizeof(tapes)); // stale audio a fresh tape must not carry
	}
};
TestHeap heap;

void* test_allocate(std::size_t bytes) {
	if (heap.fail || bytes > kTapeBytes) {
		return nullptr;
	}
	for (int i = 0; i < 2; ++i) {
		if (!heap.in_use[i]) {
			heap.in_use[i] = true;
			++heap.allocations;
			return heap.tapes[i].data();
		}
	}
	return nullptr;
}

void test_release(void* block) {
	for (int i = 0; i < 2; ++i) {
		if (block == heap.tapes[i].data()) {
			if (!heap.in_use[i]) {
				++heap.double_releases;
			}
			heap.in_use[i] = false;
			++heap.releases;
		}
	}
}

constexpr RingAllocator kAllocator{.allocate = test_allocate, .release = test_release};

/// @brief The component with the reference's calls: begun by prepare, commit and finish, as its owner hands the tape
/// over.
class Owner {
public:
	Owner() : fx_(kAllocator) {}

	void begin(uint32_t rate, const StutterState& settings, StereoSample /*fill*/ = {}) {
		StutterChange change = fx_.prepare(rate);
		std::ignore = fx_.commit(change, settings);
		fx_.finish(change);
	}
	void process(std::span<StereoSample> audio, uint32_t rate) { fx_.render(audio, rate); }
	[[nodiscard]] bool playing() const { return fx_.state().playing; }
	[[nodiscard]] bool reversed() const { return fx_.state().reverse; }
	[[nodiscard]] int32_t frames_left() const { return fx_.state().frames_left; }
	[[nodiscard]] const deluge::dsp::fixed::DelayTape& tape() const { return fx_.tape(); }

private:
	Stutter fx_;
};

const StutterState kForward{.quantized = false};

} // namespace

// clang-format off
describe stutter("effects::Stutter", ${
	before_each([] { heap.reset(); });

	it("renders each workload as the reference recorded it", _ {
		for (const sw::Workload& workload : sw::all_workloads()) {
			heap.reset();
			expect(sw::record<Owner>(workload)).to_equal(sw::kRecordedReference.at(static_cast<std::size_t>(workload.id)));
		}
	});

	it("passes the block through, untouched, while inactive", _ {
		Stutter fx{kAllocator};
		std::array<StereoSample, 4> block{{{1, 2}, {3, 4}, {5, 6}, {7, 8}}};
		const std::array<StereoSample, 4> before = block;
		fx.render(block, sw::kRate);
		expect(fx.active()).to_be_false();
		for (std::size_t i = 0; i < block.size(); ++i) {
			expect(block[i].l).to_equal(before[i].l);
			expect(block[i].r).to_equal(before[i].r);
		}
	});

	it("fails the commit and stays inactive when the tape cannot be allocated", _ {
		Stutter fx{kAllocator};
		heap.fail = true;
		StutterChange change = fx.prepare(sw::kRate);
		expect(change.acquired).to_be_null();
		expect(fx.commit(change, kForward)).to_be_false();
		fx.finish(change);
		expect(fx.active()).to_be_false();
		expect(heap.allocations).to_equal(0);
		expect(heap.releases).to_equal(0);
	});

	it("moves the tape it holds to released when a second commit begins anew", _ {
		Stutter fx{kAllocator};
		StutterChange first = fx.prepare(sw::kRate);
		expect(fx.commit(first, kForward)).to_be_true();
		fx.finish(first);
		const deluge::dsp::fixed::DelayTape& held = fx.tape();
		StereoSample* const first_storage = held.begin();

		StutterChange second = fx.prepare(sw::kRate / 2);
		expect(fx.commit(second, kForward)).to_be_true();
		expect(second.released).to_equal(first_storage);
		expect(fx.active()).to_be_true();
		expect(fx.state().playing).to_be_false();
		fx.finish(second);
		expect(heap.releases).to_equal(1);
		expect(heap.live()).to_equal(1);
	});

	it("takes the tape back at the end, and finish frees each tape once", _ {
		{
			Stutter fx{kAllocator};
			StutterChange begin = fx.prepare(sw::kRate);
			expect(fx.commit(begin, kForward)).to_be_true();
			fx.finish(begin);
			StutterChange end{};
			fx.commit_end(end);
			expect(fx.active()).to_be_false();
			expect(end.released).not_().to_be_null();
			fx.finish(end);
			expect(end.released).to_be_null();
			fx.finish(end);
		}
		expect(heap.allocations).to_equal(1);
		expect(heap.releases).to_equal(1);
		expect(heap.double_releases).to_equal(0);
	});

	it("frees the tape it holds when destroyed", _ {
		{
			Stutter fx{kAllocator};
			StutterChange begin = fx.prepare(sw::kRate);
			expect(fx.commit(begin, kForward)).to_be_true();
			fx.finish(begin);
		}
		expect(heap.releases).to_equal(1);
		expect(heap.live()).to_equal(0);
	});

	it("frees a tape prepare allocated that commit never took", _ {
		{
			Stutter fx{kAllocator};
			StutterChange unused = fx.prepare(sw::kRate);
			fx.finish(unused);
			expect(fx.active()).to_be_false();
		}
		expect(heap.allocations).to_equal(1);
		expect(heap.releases).to_equal(1);
	});
});
// clang-format on

CPPSPEC_SPEC(stutter)
