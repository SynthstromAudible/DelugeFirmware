// peak_cache.h against the legacy scan's prelude: a cache keeps, slides and clears its columns as the oracle's prelude
// does, reads exactly what it lacks, and the set keeps one cache per waveform.
#include "sample-view/peak_cache.h"

#include "legacy_oracle.h"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::sample_view;
namespace oracle = deluge::sample_view::test::oracle;

namespace {

/// @brief The pattern a cached window starts with: past the end on every third column, a peak `{-c, c}` on the other
///        even ones, and pending elsewhere.
uint8_t pattern(int32_t c) {
	if (c % 3 == 0) {
		return oracle::kBeyond;
	}
	return c % 2 == 0 ? oracle::kInvestigated : oracle::kNotInvestigated;
}

PeakRead pattern_read() {
	PeakRead read;
	for (int32_t c = 0; c < kColumns; c++) {
		switch (pattern(c)) {
		case oracle::kBeyond:
			read.set(c, ColumnRead::past_end());
			break;
		case oracle::kInvestigated:
			read.set(c, ColumnRead::of({.min = -c, .max = c}));
			break;
		default:
			break;
		}
	}
	return read;
}

oracle::RenderData pattern_data(int64_t scroll, int64_t zoom, int64_t length) {
	oracle::RenderData data{.xScroll = scroll, .xZoom = zoom, .validLengthSamples = length};
	for (int32_t c = 0; c < kColumns; c++) {
		data.colStatus[c] = pattern(c);
		if (pattern(c) == oracle::kInvestigated) {
			data.minPerCol[c] = -c;
			data.maxPerCol[c] = c;
		}
	}
	return data;
}

/// @brief A reader that answers every column it is asked for with @p answer, and counts its calls and the columns
///        it was last asked for.
struct Reader {
	PeakRead answer;
	int32_t calls = 0;
	ColumnMask last;
	PeakRead operator()(ColumnMask wanted) {
		calls++;
		last = wanted;
		return answer;
	}
};

PeakRead all_peaks(int32_t base) {
	PeakRead read;
	for (int32_t c = 0; c < kColumns; c++) {
		read.set(c, ColumnRead::of({.min = base - c, .max = base + c}));
	}
	return read;
}

} // namespace

// clang-format off
describe peak_cache_group("peak_cache.h", ${
	it("keeps, slides and clears its columns as the oracle's prelude does", _ {
		const std::vector<int64_t> scrolls{-1, 0, 1, 7, 15, 16, 17, 31, 1000};
		const std::vector<int64_t> zooms{1, 2, 3, 16, 1024};
		constexpr int64_t kLength = 100'000;
		int64_t compared = 0;
		int64_t failures = 0;
		for (const int64_t s : scrolls) {
			for (const int64_t z : zooms) {
				std::vector<int64_t> targets{s + 1, s - 1};
				for (int64_t k = -17; k <= 17; k++) {
					targets.push_back(s + k * z);
				}
				for (const int64_t s2 : targets) {
					for (const int64_t z2 : {z, 2 * z}) {
						for (const int64_t l2 : {kLength, kLength + 7}) {
							for (const bool growing : {false, true}) {
								compared++;
								PeakCache cache;
								Reader setup{.answer = pattern_read()};
								(void)cache.update({.scroll = s, .zoom = z}, kLength, false, setup);
								Reader nothing{};
								const PeakRead kept = cache.update({.scroll = s2, .zoom = z2}, l2, growing, nothing);
								oracle::RenderData data = pattern_data(s, z, kLength);
								oracle::prelude(data, s2, static_cast<uint64_t>(z2), static_cast<uint64_t>(l2), growing);
								bool same = cache.window() == Window{.scroll = data.xScroll, .zoom = data.xZoom};
								for (int32_t c = 0; c < kColumns; c++) {
									const bool investigated = data.colStatus[c] == oracle::kInvestigated;
									const std::optional<Peak> p = kept.column(c).peak();
									same = same && p.has_value() == investigated
									       && (!p || (p->min == data.minPerCol[c] && p->max == data.maxPerCol[c]));
								}
								if (!same && failures++ < 10) {
									std::cout << "prelude {" << s << ", " << z << "} to {" << s2 << ", " << z2
									          << "} length " << l2 << " growing " << growing << "\n";
								}
							}
						}
					}
				}
			}
		}
		std::cout << "prelude: " << compared << " compared\n";
		expect(failures).to_equal(0);
	});
	it("slides by fifteen columns, keeping the one column still in view", _ {
		for (const int32_t shift : {15, -15}) {
			PeakCache cache;
			Reader setup{.answer = all_peaks(100)};
			(void)cache.update({.scroll = 1000, .zoom = 4}, 100'000, false, setup);
			Reader r{};
			const PeakRead read = cache.update({.scroll = 1000 + 4 * shift, .zoom = 4}, 100'000, false, r);
			const int32_t kept = shift > 0 ? 0 : kColumns - 1;
			expect(read.column(kept)).to_equal(setup.answer.column(kept + shift));
			expect(r.last.count()).to_equal(size_t{kColumns - 1});
		}
	});
	it("clears on any update, made fresh or forgotten", _ {
		PeakCache fresh;
		Reader r{};
		(void)fresh.update({.scroll = 0, .zoom = 1}, 100, false, r);
		expect(r.calls).to_equal(1);
		expect(r.last.all()).to_be_true();
		PeakCache forgotten;
		Reader full{.answer = all_peaks(0)};
		(void)forgotten.update({.scroll = 0, .zoom = 1}, 100, false, full);
		forgotten.forget();
		expect(forgotten.window()).not_().to_have_value();
		Reader again{};
		(void)forgotten.update({.scroll = 0, .zoom = 1}, 100, false, again);
		expect(again.last.all()).to_be_true();
	});
	it("asks once for exactly the columns it lacks in its range, past-the-end ones included (Q17, kept)", _ {
		PeakCache cache;
		Reader setup{.answer = pattern_read()};
		(void)cache.update({.scroll = 0, .zoom = 4}, 1000, false, setup);
		Reader r{};
		(void)cache.update({.scroll = 0, .zoom = 4}, 1000, false, r, 2, 12);
		expect(r.calls).to_equal(1);
		for (int32_t c = 0; c < kColumns; c++) {
			const bool want = c >= 2 && c < 12 && pattern(c) != oracle::kInvestigated;
			expect(r.last.test(static_cast<size_t>(c))).to_equal(want);
		}
		// The oracle's scan re-reads a BEYOND column too: it skips only INVESTIGATED ones.
		expect(pattern(3)).to_equal(oracle::kBeyond);
		expect(r.last.test(3)).to_be_true();
		PeakCache whole;
		Reader all{.answer = all_peaks(5)};
		(void)whole.update({.scroll = 0, .zoom = 4}, 1000, false, all);
		Reader none{};
		(void)whole.update({.scroll = 0, .zoom = 4}, 1000, false, none);
		expect(none.calls).to_equal(1);
		expect(none.last.none()).to_be_true();
	});
	it("takes the reader's answer for every column asked, and leaves the others", _ {
		PeakCache cache;
		Reader setup{.answer = all_peaks(0)};
		(void)cache.update({.scroll = 0, .zoom = 4}, 1000, false, setup);
		const PeakRead before = cache.columns();
		Reader again{.answer = PeakRead::all_past_end()};
		const PeakRead after = cache.update({.scroll = 4 * 3, .zoom = 4}, 1000, false, again);
		for (int32_t c = 0; c < kColumns; c++) {
			if (c < kColumns - 3) {
				expect(after.column(c)).to_equal(before.column(c + 3));
			}
			else {
				expect(after.column(c).state()).to_equal(ColumnState::PAST_END);
			}
		}
		PeakCache partial;
		Reader pend{.answer = PeakRead{}};
		const PeakRead p = partial.update({.scroll = 0, .zoom = 1}, 100, false, pend);
		expect(p.pending()).to_be_true();
	});
	it("keeps a growing take's columns, and clears them when the take ends", _ {
		PeakCache cache;
		Reader setup{.answer = all_peaks(0)};
		(void)cache.update({.scroll = 0, .zoom = 4}, 64, true, setup);
		Reader grow{};
		(void)cache.update({.scroll = 0, .zoom = 4}, 80, true, grow);
		expect(grow.last.none()).to_be_true();
		Reader ended{};
		(void)cache.update({.scroll = 0, .zoom = 4}, 80, false, ended);
		expect(ended.last.all()).to_be_true();
	});
	it("Q1: a forgotten cache reads every column again instead of sliding", _ {
		PeakCache cache;
		Reader setup{.answer = all_peaks(0)};
		(void)cache.update({.scroll = 5, .zoom = 1}, 1000, false, setup);
		cache.forget();
		Reader r{};
		(void)cache.update({.scroll = 5, .zoom = 1}, 1000, false, r);
		expect(r.last.all()).to_be_true();
		// The oracle forgets by a scroll of -1, and its prelude then slides by 6.
		oracle::RenderData data{.xScroll = 5, .xZoom = 1, .validLengthSamples = 1000};
		for (int32_t c = 0; c < kColumns; c++) {
			data.colStatus[c] = oracle::kInvestigated;
			data.maxPerCol[c] = c;
		}
		data.xScroll = -1;
		oracle::prelude(data, 5, 1, 1000, false);
		for (int32_t c = 0; c < kColumns; c++) {
			expect(data.colStatus[c]).to_equal(c < 10 ? oracle::kInvestigated : oracle::kNotInvestigated);
			if (c < 10) {
				expect(data.maxPerCol[c]).to_equal(c + 6);
			}
		}
	});
	it("Q3: a zoom below 1 covers no frames, reads nothing and keeps nothing", _ {
		PeakCache cache;
		Reader setup{.answer = all_peaks(0)};
		(void)cache.update({.scroll = 0, .zoom = 1}, 1000, false, setup);
		Reader r{};
		const PeakRead read = cache.update({.scroll = 5, .zoom = 0}, 1000, false, r);
		expect(read).to_equal(PeakRead::all_past_end());
		expect(r.calls).to_equal(0);
		for (int32_t c = 0; c < kColumns; c++) {
			expect(cache.columns().column(c).peak()).not_().to_have_value();
		}
	});
	it("keeps one cache per waveform, giving up the one read least lately", _ {
		PeakCacheSet<4> set;
		const Window w{.scroll = 0, .zoom = 4};
		const auto read = [&](uint64_t id, int32_t base) {
			Reader r{.answer = all_peaks(base)};
			const PeakRead got = set.read(id, w, 1000, false, r);
			return std::pair{got, r.last};
		};
		expect(read(1, 10).second.all()).to_be_true();
		expect(read(2, 20).second.all()).to_be_true();
		expect(set.held()).to_equal(size_t{2});
		expect(read(1, 99).first.column(0).peak()->max).to_equal(10);
		expect(read(3, 30).second.all()).to_be_true();
		expect(read(4, 40).second.all()).to_be_true();
		expect(set.held()).to_equal(size_t{4});
		// Read order now 2, 1, 3, 4; reading 1 again leaves 2 the least lately read.
		(void)read(1, 0);
		expect(read(5, 50).second.all()).to_be_true();
		expect(set.held()).to_equal(size_t{4});
		expect(read(3, 0).second.none()).to_be_true();
		expect(read(2, 21).second.all()).to_be_true(); // given up, so read afresh
	});
	it("Q29: a waveform read again at its window reads nothing, whichever was read between", _ {
		PeakCacheSet<4> set;
		const Window w{.scroll = 32, .zoom = 2};
		Reader a{.answer = all_peaks(7)};
		const PeakRead first = set.read(0xA, w, 1000, false, a);
		Reader b{.answer = all_peaks(9)};
		(void)set.read(0xB, w, 1000, false, b);
		Reader again{.answer = PeakRead::all_past_end()};
		const PeakRead second = set.read(0xA, w, 1000, false, again);
		expect(again.last.none()).to_be_true();
		expect(second).to_equal(first);
	});
});
// clang-format on

DELUGE_SPEC(peak_cache_spec, peak_cache_group)
