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

#include "deluge_storage_sample_peaks.h"
#include "test/peaks_test_support.h"

#include "cppspec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

// The peak index driven through its C ABI from C++: the calls the app makes, the answers it reads.

namespace {

void* host_alloc(size_t size, size_t align) {
	const size_t a = align < sizeof(void*) ? sizeof(void*) : align;
	return std::aligned_alloc(a, (size + a - 1) / a * a);
}

void host_free(void* p) {
	std::free(p);
}

/// An index of `frames` frames, released and collected when it goes.
struct Index {
	DelugePeakIndex* p;
	explicit Index(uint64_t frames) : p{deluge_peaks_create(frames, false, frames)} {}
	Index(const Index&) = delete;
	Index& operator=(const Index&) = delete;
	~Index() {
		deluge_peaks_release(p);
		deluge_peaks_collect();
	}
};

} // namespace

// clang-format off
describe peaks_abi("the peak index's C ABI", $ {
	before_all([] { deluge_peaks_init(host_alloc, host_free); });

	it("answers a column from folded frames", _ {
		std::array<int16_t, 1024> frames{};
		for (size_t i = 0; i < frames.size(); i++) {
			frames[i] = static_cast<int16_t>(i % 7 == 0 ? -1000 : 300);
		}
		frames[600] = 32000;
		Index idx{frames.size()};
		expect(idx.p).not_().to_be_null();
		deluge_peaks_fold_frames(idx.p, 0, reinterpret_cast<const uint8_t*>(frames.data()), sizeof frames, 2, 1);
		const DelugePeaksColumn c = deluge_peaks_column(idx.p, 0, frames.size());
		expect(c.pending).to_equal(false);
		expect(c.read_count).to_equal(uint8_t{0});
		expect(c.interior.min).to_equal(int16_t{-1000});
		expect(c.interior.max).to_equal(int16_t{32000});
		const DelugeMaybePeak range = deluge_peaks_range(idx.p);
		expect(range.has).to_equal(true);
		expect(range.peak.max).to_equal(int16_t{32000});
	});

	it("is pending until a block is folded", _ {
		Index idx{4096};
		expect(deluge_peaks_column(idx.p, 0, 4096).pending).to_equal(true);
		expect(deluge_peaks_next_unscanned(idx.p, 0)).to_equal(uint64_t{0});
		expect(deluge_peaks_range(idx.p).has).to_equal(false);
	});

	it("hands a column's ends back as frame ranges to read", _ {
		std::array<int16_t, 8192> frames{};
		Index idx{frames.size()};
		deluge_peaks_fold_frames(idx.p, 0, reinterpret_cast<const uint8_t*>(frames.data()), sizeof frames, 2, 1);
		// No level-0 values kept: the two level-1 entries the span covers in part are read, as one merged range.
		const DelugePeaksColumn c = deluge_peaks_column(idx.p, 100, 5000);
		expect(c.pending).to_equal(false);
		expect(c.read_count).to_equal(uint8_t{1});
		expect(c.read_lo[0]).to_equal(uint64_t{100});
		expect(c.read_hi[0]).to_equal(uint64_t{5000});
	});

	it("reads only the part-covered blocks once level-0 values are kept", _ {
		std::array<int16_t, 8192> frames{};
		Index idx{frames.size()};
		deluge_peaks_ensure_level0(idx.p, 0, frames.size());
		deluge_peaks_fold_frames(idx.p, 0, reinterpret_cast<const uint8_t*>(frames.data()), sizeof frames, 2, 1);
		const DelugePeaksColumn c = deluge_peaks_column(idx.p, 100, 5000);
		expect(c.read_count).to_equal(uint8_t{2});
		expect(c.read_lo[0]).to_equal(uint64_t{100});
		expect(c.read_hi[0]).to_equal(uint64_t{256});
		expect(c.read_lo[1]).to_equal(uint64_t{4864});
		expect(c.read_hi[1]).to_equal(uint64_t{5000});
	});

	it("finds a block through the asset it is bound to, and drops it when unbound", _ {
		Index idx{1024};
		deluge_peaks_bind_asset(7, idx.p);
		deluge_peaks_unbind_asset(7);
		// The binding's reference is gone; the index is still the spec's.
		expect(deluge_peaks_next_unscanned(idx.p, 0)).to_equal(uint64_t{0});
	});

	it("fills uniformly for test seeding", _ {
		Index idx{uint64_t{1} << 33};
		deluge_peaks_fill_uniform(idx.p, DelugePeak{.min = -128, .max = 127});
		const uint64_t lo = uint64_t{1} << 31;
		const DelugePeaksColumn c = deluge_peaks_column(idx.p, lo, lo + (uint64_t{1} << 28) + 77);
		expect(c.pending).to_equal(false);
		expect(c.interior.max).to_equal(int16_t{127});
		expect(deluge_peaks_next_unscanned(idx.p, 0)).to_equal(UINT64_MAX);
		expect(deluge_peaks_range(idx.p).has).to_equal(true);
	});

	it("writes a sidecar with another section, checks it, finds its sections and restores its peaks", _ {
		constexpr uint64_t kFrames = 100'000;
		static_assert(offsetof(DelugePeakKey, frames) == 16);
		const DelugePeakKey key{.first_cluster = 0x9A12C4E0, .file_size = 1'234'567, .fat_datetime = 0x5A216C33,
		                        .sample_rate = 44'100, .frames = kFrames, .channels = 2, .byte_depth = 3, .reserved = 0};
		Index source{kFrames};
		for (uint64_t b = 0; b < 20; b++) {
			deluge_peaks_fold_block(source.p, b, DelugePeak{.min = -7, .max = 9});
		}
		expect(deluge_peaks_level1_entries(source.p)).to_equal(uint32_t{25});
		expect(deluge_peaks_complete_level1(source.p)).to_equal(uint32_t{1});
		constexpr std::array<uint8_t, 6> kTest{'a', 'b', 'c', 'd', 'e', 'f'};
		constexpr uint32_t kTagTest = 'T' | ('E' << 8) | ('S' << 16) | ('T' << 24);
		const DelugeSidecarSection test{.tag = kTagTest, .bytes = kTest.data(), .len = kTest.size()};
		std::vector<uint8_t> bytes(deluge_peaks_sidecar_len(source.p, &test, 1));
		expect(bytes.size()).to_equal(size_t{64 + 2 * 16 + 8 + 25 * 4 + 6});
		expect(deluge_peaks_sidecar_write(source.p, &key, &test, 1, bytes.data(), bytes.size()))
		    .to_equal(static_cast<uint32_t>(bytes.size()));
		const auto n = static_cast<uint32_t>(bytes.size());
		expect(deluge_peaks_sidecar_check(bytes.data(), n, &key)).to_equal(0);
		const uint8_t* found = nullptr;
		uint32_t foundLen = 0;
		expect(deluge_peaks_sidecar_section(bytes.data(), n, &key, kTagTest, &found, &foundLen)).to_equal(true);
		expect(std::vector<uint8_t>(found, found + foundLen)).to_equal(std::vector<uint8_t>(kTest.begin(), kTest.end()));
		Index fresh{kFrames};
		expect(deluge_peaks_sidecar_restore(fresh.p, &key, bytes.data(), n)).to_equal(1);
		expect(deluge_peaks_next_unscanned(fresh.p, 0)).to_equal(uint64_t{16 * 256});
		bytes[0] ^= 0x40;
		expect(deluge_peaks_sidecar_check(bytes.data(), n, &key)).to_equal(-2);
		expect(deluge_peaks_sidecar_write(source.p, &key, &test, 1, bytes.data(), n - 1)).to_equal(uint32_t{0});
	});

	it("keeps level 0 only around the kept window", _ {
		constexpr uint64_t kChunkFrames = uint64_t{1} << 18;
		Index idx{8 * kChunkFrames};
		deluge_peaks_ensure_level0(idx.p, 0, 8 * kChunkFrames);
		expect(deluge_peaks_level0_chunks(idx.p)).to_equal(uint32_t{8});
		deluge_peaks_keep_level0(idx.p, 3 * kChunkFrames + 10, 3 * kChunkFrames + 70'000, 1'000);
		expect(deluge_peaks_trim_level0(idx.p, 1'500)).to_equal(uint32_t{7});
		expect(deluge_peaks_level0_chunks(idx.p)).to_equal(uint32_t{1});
		expect(deluge_peaks_trim_level0(idx.p, 2'001 + 1'000)).to_equal(uint32_t{1});
		expect(deluge_peaks_level0_chunks(idx.p)).to_equal(uint32_t{0});
	});

	it("treats a null index as nothing", _ {
		expect(deluge_peaks_column(nullptr, 0, 10).pending).to_equal(true);
		expect(deluge_peaks_range(nullptr).has).to_equal(false);
		expect(deluge_peaks_next_unscanned(nullptr, 0)).to_equal(UINT64_MAX);
		deluge_peaks_release(nullptr);
		expect(deluge_peaks_sidecar_len(nullptr, nullptr, 0)).to_equal(uint32_t{0});
		expect(deluge_peaks_sidecar_restore(nullptr, nullptr, nullptr, 0)).to_equal(-1);
		expect(deluge_peaks_level0_chunks(nullptr)).to_equal(uint32_t{0});
	});
});
// clang-format on

CPPSPEC_SPEC(peaks_abi)
