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

#include "model/sample/take_peaks.h"

#include "deluge_storage_sample_peaks.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

// A take's peaks folded cluster by cluster, against brute force over the same bytes.

namespace {

void* host_alloc(size_t size, size_t align) {
	const size_t a = align < sizeof(void*) ? sizeof(void*) : align;
	return std::aligned_alloc(a, (size + a - 1) / a * a);
}

void host_free(void* p) {
	std::free(p);
}

/// A recorded file: `header` bytes, then `frames` 24-bit frames of `channels` channels, pseudo-random.
std::vector<uint8_t> file_of(uint32_t header, uint32_t frames, uint8_t channels, uint32_t seed) {
	std::vector<uint8_t> f(header + frames * 3u * channels, 0xee);
	uint32_t s = seed | 1;
	for (size_t i = header; i < f.size(); i++) {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		f[i] = static_cast<uint8_t>(s);
	}
	return f;
}

DelugePeak brute(const std::vector<uint8_t>& f, uint32_t header, uint8_t channels, uint64_t first, uint64_t end) {
	DelugePeak p{.min = INT16_MAX, .max = INT16_MIN};
	for (uint64_t i = first; i < end; i++) {
		for (uint8_t c = 0; c < channels; c++) {
			const size_t off = header + (i * channels + c) * 3;
			const auto v = static_cast<int16_t>(static_cast<uint16_t>(f[off + 1] | (f[off + 2] << 8)));
			p = {.min = std::min(p.min, v), .max = std::max(p.max, v)};
		}
	}
	return p;
}

/// Feeds `f` in pieces of the given sizes (cycled), finishes, and checks every block.
bool every_block_matches(uint32_t header, uint32_t frames, uint8_t channels, std::vector<uint32_t> pieces) {
	const std::vector<uint8_t> f = file_of(header, frames, channels, frames * 31 + header);
	DelugePeakIndex* idx = deluge_peaks_create(0, true, uint64_t{1} << 24);
	deluge_peaks_reserve(idx, frames);
	deluge_peaks_ensure_level0(idx, 0, frames);
	deluge::sample::TakePeaks take;
	size_t at = 0;
	for (size_t n = 0; at < f.size(); n++) {
		const size_t len = std::min<size_t>(pieces[n % pieces.size()], f.size() - at);
		take.feed(idx, {f.data() + at, len}, header, channels);
		at += len;
	}
	deluge_peaks_set_frames(idx, frames, true);
	take.finish(idx);
	bool ok = take.frames() == frames && deluge_peaks_next_unscanned(idx, 0) == UINT64_MAX;
	for (uint64_t b = 0; b * 256 < frames; b++) {
		const uint64_t end = std::min<uint64_t>((b + 1) * 256, frames);
		const DelugePeaksColumn c = deluge_peaks_column(idx, b * 256, end);
		const DelugePeak want = brute(f, header, channels, b * 256, end);
		ok = ok && !c.pending && c.read_count == 0 && c.interior.min == want.min && c.interior.max == want.max;
	}
	deluge_peaks_release(idx);
	deluge_peaks_collect();
	return ok;
}

} // namespace

// clang-format off
describe take_peaks("a take's peaks, folded as its clusters are written", $ {
	before_all([] { deluge_peaks_init(host_alloc, host_free); });

	it("folds whole clusters of mono frames after a 44-byte header", _ {
		expect(every_block_matches(44, 5000, 1, {2048})).to_equal(true);
	});

	it("carries a frame split across clusters, stereo, after a 112-byte header", _ {
		// 1,000-byte pieces split 6-byte frames every time but one in three.
		expect(every_block_matches(112, 4321, 2, {1000})).to_equal(true);
	});

	it("handles a header longer than the first piece, and one-byte pieces", _ {
		expect(every_block_matches(112, 700, 2, {7, 1, 30, 1, 1, 2})).to_equal(true);
	});

	it("folds the last, part-filled block only when finished", _ {
		const std::vector<uint8_t> f = file_of(44, 300, 1, 9);
		DelugePeakIndex* idx = deluge_peaks_create(0, true, 4096);
		deluge_peaks_reserve(idx, 300);
		deluge::sample::TakePeaks take;
		take.feed(idx, f, 44, 1);
		deluge_peaks_set_frames(idx, 300, false);
		expect(deluge_peaks_next_unscanned(idx, 0)).to_equal(uint64_t{256});
		deluge_peaks_set_frames(idx, 300, true);
		take.finish(idx);
		expect(deluge_peaks_next_unscanned(idx, 0)).to_equal(UINT64_MAX);
		deluge_peaks_release(idx);
		deluge_peaks_collect();
	});
});
// clang-format on

CPPSPEC_SPEC(take_peaks)
