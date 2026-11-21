// The test-only transcription of the legacy waveform arithmetic (tests/unit/sample-view/legacy_oracle.h) against the
// live legacy code: the scan's prelude, the bar and single-row arithmetic and the navigator give the same answers over
// the domains the unit specs compare the new types in. A scan whose recorder is growing a take is covered by the unit
// spec alone: no recorder here constructs without arming a take.

#include "model/sample/sample.h"
#include "model/sample/waveform_navigator.h"
#include "model/sample/waveform_render_data.h"
#include "model/sample/waveform_scan.h"
#include "storage/cluster/cluster.h"

#include "unit/sample-view/legacy_oracle.h"
#include "unit/sample-view/nav_cases.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <new>
#include <vector>

namespace oracle = deluge::sample_view::test::oracle;
using deluge::sample_view::MarkerPin;
using deluge::sample_view::test::nav_opens;
using deluge::sample_view::test::nav_starts;
using deluge::sample_view::test::nav_zoom_cases;
using deluge::sample_view::test::NavCase;
using deluge::sample_view::test::NavOpen;
using deluge::sample_view::test::NavStart;

namespace {

constexpr size_t kClusterSize = 32768;
constexpr int32_t kNumClusters = 8;
constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * kNumClusters / 2; // 16-bit mono

/// A loaded-looking sample, for the prelude alone: a scan of no columns reads none of its frames.
Sample* make_sample() {
	Cluster::set_size(kClusterSize);
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = kSampleLength * 2;
	s->lengthInSamples = kSampleLength;
	s->resizeOverviewCache(kNumClusters);
	return s;
}

uint8_t pattern(int32_t c) {
	if (c % 3 == 0) {
		return oracle::kBeyond;
	}
	return c % 2 == 0 ? oracle::kInvestigated : oracle::kNotInvestigated;
}

const std::vector<int32_t> kValues{INT32_MIN / 2, -1, 0, 1, (1 << 23) - 1, 1 << 23, 1 << 24, INT32_MAX / 2};

std::array<MarkerColumn, 4> legacy_cols(const std::array<MarkerPin, 4>& pins) {
	std::array<MarkerColumn, 4> cols{};
	for (size_t m = 0; m < 4; m++) {
		cols[m] = {.pos = static_cast<int32_t>(pins[m].position),
		           .colOnScreen = pins[m].column ? static_cast<int32_t>(*pins[m].column) : oracle::kInvalidColumn};
	}
	return cols;
}

std::array<oracle::MarkerColumn, 4> oracle_cols(const std::array<MarkerColumn, 4>& cols) {
	std::array<oracle::MarkerColumn, 4> out{};
	for (size_t m = 0; m < 4; m++) {
		out[m] = {.pos = cols[m].pos, .colOnScreen = cols[m].colOnScreen};
	}
	return out;
}

} // namespace

// clang-format off
describe sample_view_oracle("the legacy waveform arithmetic's transcription", ${
	it("runs the scan's prelude as find_peaks_per_col does", _ {
		Sample* sample = make_sample();
		const std::vector<int64_t> scrolls{-1, 0, 1, 7, 15, 16, 17, 31, 1000};
		const std::vector<int64_t> zooms{1, 2, 3, 16, 1024};
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
						for (const int64_t cached_length : {static_cast<int64_t>(kSampleLength),
						                                    static_cast<int64_t>(kSampleLength) - 5}) {
							WaveformRenderData legacy{};
							oracle::RenderData copy{.xScroll = s, .xZoom = z, .validLengthSamples = cached_length};
							legacy.xScroll = s;
							legacy.xZoom = z;
							legacy.validLengthSamples = cached_length;
							for (int32_t c = 0; c < 16; c++) {
								legacy.colStatus[c] = copy.colStatus[c] = pattern(c);
								legacy.minPerCol[c] = copy.minPerCol[c] = -c;
								legacy.maxPerCol[c] = copy.maxPerCol[c] = c;
							}
							(void)deluge::sample::find_peaks_per_col(sample, s2, static_cast<uint64_t>(z2), &legacy,
							                                         nullptr, 0, 0);
							oracle::prelude(copy, s2, static_cast<uint64_t>(z2), kSampleLength, false);
							compared++;
							bool same = legacy.xScroll == copy.xScroll && legacy.xZoom == copy.xZoom
							            && legacy.validLengthSamples == copy.validLengthSamples;
							for (int32_t c = 0; c < 16; c++) {
								same = same && legacy.colStatus[c] == copy.colStatus[c]
								       && legacy.minPerCol[c] == copy.minPerCol[c]
								       && legacy.maxPerCol[c] == copy.maxPerCol[c];
							}
							if (!same && failures++ < 10) {
								std::cout << "prelude {" << s << ", " << z << "} to {" << s2 << ", " << z2 << "}\n";
							}
						}
					}
				}
			}
		}
		std::cout << "prelude: " << compared << " compared\n";
		expect(failures).to_equal(0);
	});
	it("draws bars and single-row brightness as the legacy scan does", _ {
		int64_t failures = 0;
		for (const int32_t low : kValues) {
			for (const int32_t high : kValues) {
				if (low > high) {
					continue;
				}
				WaveformRenderData legacy{};
				oracle::RenderData copy{};
				legacy.minPerCol[0] = copy.minPerCol[0] = low;
				legacy.maxPerCol[0] = copy.maxPerCol[0] = high;
				for (const int32_t centre : kValues) {
					for (const int32_t span : {1, 3, 1 << 24, INT32_MAX}) {
						int32_t lmin = 0;
						int32_t lmax = 0;
						int32_t omin = 0;
						int32_t omax = 0;
						deluge::sample::col_bar_positions(0, &legacy, &lmin, &lmax, centre, span);
						oracle::col_bar_positions(0, copy, omin, omax, centre, span);
						failures += (lmin != omin || lmax != omax) ? 1 : 0;
					}
				}
				for (const int32_t max_peak : kValues) {
					if (max_peak > 0) {
						failures += deluge::sample::col_brightness_for_single_row(0, max_peak, &legacy)
						                    != oracle::col_brightness_for_single_row(0, max_peak, copy)
						                ? 1
						                : 0;
					}
				}
			}
		}
		expect(failures).to_equal(0);
	});
	it("opens, measures, clamps, scrolls and zooms as WaveformNav does", _ {
		int64_t failures = 0;
		for (const NavOpen& o : nav_opens()) {
			deluge::sample::WaveformNav nav{};
			nav.length_samples = o.length;
			nav.opened(o.scroll, o.zoom);
			oracle::Nav copy{.length = o.length};
			copy.opened(o.scroll, o.zoom);
			failures += (nav.xScroll != copy.xScroll || nav.xZoom != copy.xZoom) ? 1 : 0;
		}
		for (const NavStart& s : nav_starts()) {
			const auto fresh = [&] {
				deluge::sample::WaveformNav nav{};
				nav.length_samples = s.length;
				nav.xScroll = s.scroll;
				nav.xZoom = s.zoom;
				return nav;
			};
			const oracle::Nav start{.length = s.length, .xZoom = s.zoom, .xScroll = s.scroll};
			const deluge::sample::WaveformNav nav = fresh();
			failures += (nav.max_zoom() != start.max_zoom() || nav.is_zoomed_in() != start.is_zoomed_in()) ? 1 : 0;
			for (const bool extra : {false, true}) {
				deluge::sample::WaveformNav clamped = fresh();
				clamped.adjust_scroll_position(extra);
				oracle::Nav copy = start;
				copy.adjust_scroll_position(extra);
				failures += clamped.xScroll != copy.xScroll ? 1 : 0;
				for (const int32_t offset : {-1, 1}) {
					for (const int32_t end : {oracle::kInvalidColumn, 15, 16}) {
						std::array<MarkerColumn, 4> cols{};
						for (auto& c : cols) {
							c = {.pos = 0, .colOnScreen = oracle::kInvalidColumn};
						}
						cols[3].colOnScreen = end;
						const std::array<oracle::MarkerColumn, 4> ocols = oracle_cols(cols);
						deluge::sample::WaveformNav scrolled = fresh();
						oracle::Nav oscrolled = start;
						const bool moved = scrolled.scroll(offset, extra, cols.data());
						const bool omoved = oscrolled.scroll(offset, extra, ocols.data());
						failures += (moved != omoved || scrolled.xScroll != oscrolled.xScroll) ? 1 : 0;
					}
					deluge::sample::WaveformNav bare = fresh();
					oracle::Nav obare = start;
					failures += (bare.scroll(offset, extra, nullptr) != obare.scroll(offset, extra, nullptr)
					             || bare.xScroll != obare.xScroll)
					                ? 1
					                : 0;
				}
			}
		}
		for (const NavCase& c : nav_zoom_cases()) {
			deluge::sample::WaveformNav nav{};
			nav.length_samples = c.length;
			nav.xScroll = c.scroll;
			nav.xZoom = c.zoom;
			oracle::Nav copy{.length = c.length, .xZoom = c.zoom, .xScroll = c.scroll};
			const std::array<MarkerColumn, 4> cols = legacy_cols(c.pins);
			const std::array<oracle::MarkerColumn, 4> ocols = oracle_cols(cols);
			const int32_t type = c.preferred ? static_cast<int32_t>(*c.preferred) : oracle::kMarkerNone;
			const bool moved = nav.zoom(c.offset, c.extra_right, c.preferred ? cols.data() : nullptr,
			                            static_cast<MarkerType>(type));
			const bool omoved = copy.zoom(c.offset, c.extra_right, c.preferred ? ocols.data() : nullptr, type);
			const bool same = moved == omoved && nav.xScroll == copy.xScroll && nav.xZoom == copy.xZoom;
			if (!same && failures++ < 10) {
				std::cout << "zoom " << c.offset << " from " << c.length << " {" << c.scroll << ", " << c.zoom
				          << "}: legacy {" << nav.xScroll << ", " << nav.xZoom << "}, the oracle {" << copy.xScroll
				          << ", " << copy.xZoom << "}\n";
			}
		}
		expect(failures).to_equal(0);
	});
});
// clang-format on

CPPSPEC_SPEC(sample_view_oracle)
