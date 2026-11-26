// peaks.h against the legacy arithmetic's transcription: the bars and the single-row brightness agree with the oracle
// everywhere outside the Fixed-quirks rows, and each row's cases pin the correct value.
#include "ui/views/waveform/peaks.h"

#include "fixed_quirks.h"
#include "legacy_oracle.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::ui::views::waveform;
namespace oracle = deluge::ui::views::waveform::test::oracle;
using deluge::ui::views::waveform::test::bar_quirk;
using deluge::ui::views::waveform::test::brightness_quirk;
using deluge::ui::views::waveform::test::Quirk;
using deluge::ui::views::waveform::test::Skips;

namespace {

constexpr int32_t kI32Max = std::numeric_limits<int32_t>::max();
constexpr int32_t kI32Min = std::numeric_limits<int32_t>::min();

// A read stays small enough to return by value: its 16 columns of 12 bytes and its progress count.
static_assert(sizeof(PeakRead) <= 16 * 12 + sizeof(uint64_t));

const std::vector<int32_t> kValues{kI32Min / 2, -1, 0, 1, (1 << 23) - 1, 1 << 23, 1 << 24, kI32Max / 2};

/// @brief The oracle's bar for @p peak.
Bar oracle_bar(Peak peak, int32_t centre, int32_t span) {
	oracle::RenderData data;
	data.minPerCol[0] = peak.min;
	data.maxPerCol[0] = peak.max;
	int32_t min24 = 0;
	int32_t max24 = 0;
	oracle::col_bar_positions(0, data, min24, max24, centre, span);
	return {.bottom = min24, .top = max24};
}

/// @brief The oracle's single-row brightness for @p peak.
int32_t oracle_brightness(Peak peak, int32_t max_peak) {
	oracle::RenderData data;
	data.minPerCol[0] = peak.min;
	data.maxPerCol[0] = peak.max;
	return oracle::col_brightness_for_single_row(0, max_peak, data);
}

} // namespace

// clang-format off
describe peaks_group("peaks.h", ${
	it("draws every bar as the oracle does outside the listed rows", _ {
		std::vector<int32_t> peaks = kValues;
		peaks.push_back(kI32Max);
		const std::vector<int32_t> spans{1, 3, 1 << 24, kI32Max, 0, -1, -((1 << 28) - 1)};
		Skips s;
		for (const int32_t low : peaks) {
			for (const int32_t high : peaks) {
				if (low > high) {
					continue;
				}
				const Peak peak{.min = low, .max = high};
				for (const int32_t centre : kValues) {
					for (const int32_t span : spans) {
						if (const auto q = bar_quirk(peak, centre, span)) {
							s.skip(*q);
							continue;
						}
						s.compared++;
						if (bar_of(peak, centre, span) != oracle_bar(peak, centre, span)) {
							if (s.fail()) {
								std::cout << "bar {" << low << ", " << high << "} centre " << centre << " span " << span
								          << ": " << bar_of(peak, centre, span).bottom << ".."
								          << bar_of(peak, centre, span).top << " against the oracle's "
								          << oracle_bar(peak, centre, span).bottom << ".."
								          << oracle_bar(peak, centre, span).top << "\n";
							}
						}
					}
				}
			}
		}
		s.print("bars");
		expect(s.failures).to_equal(0);
		expect(s.of(Quirk::Q14)).to_be_greater_than(0);
		expect(s.of(Quirk::Q15)).to_be_greater_than(0);
	});
	it("gives every single-row brightness as the oracle does outside the listed rows", _ {
		std::vector<int32_t> peaks = kValues;
		peaks.push_back(kI32Max);
		peaks.push_back(kI32Min);
		std::vector<int32_t> max_peaks{0, 1, kI32Max};
		for (const int32_t v : kValues) {
			if (v > 0) {
				max_peaks.push_back(v);
			}
		}
		Skips s;
		for (const int32_t low : peaks) {
			for (const int32_t high : peaks) {
				if (low > high) {
					continue;
				}
				const Peak peak{.min = low, .max = high};
				for (const int32_t max_peak : max_peaks) {
					if (const auto q = brightness_quirk(peak, max_peak)) {
						s.skip(*q);
						continue;
					}
					s.compared++;
					if (single_row_brightness(peak, max_peak) != oracle_brightness(peak, max_peak)) {
						if (s.fail()) {
							std::cout << "brightness {" << low << ", " << high << "} max " << max_peak << ": "
							          << single_row_brightness(peak, max_peak) << " against the oracle's "
							          << oracle_brightness(peak, max_peak) << "\n";
						}
					}
				}
			}
		}
		s.print("brightness");
		expect(s.failures).to_equal(0);
		expect(s.of(Quirk::Q16)).to_be_greater_than(0);
	});
	it("holds a peak only in a PEAK column", _ {
		const Peak p{.min = -5, .max = 9};
		expect(ColumnRead::of(p).peak().value()).to_equal(p);
		expect(ColumnRead::of(p).state()).to_equal(ColumnState::PEAK);
		expect(ColumnRead::pending().peak()).not_().to_have_value();
		expect(ColumnRead::pending().state()).to_equal(ColumnState::PENDING);
		expect(ColumnRead::past_end().peak()).not_().to_have_value();
		expect(ColumnRead::past_end().state()).to_equal(ColumnState::PAST_END);
		expect(ColumnRead{}).to_equal(ColumnRead::pending());
	});
	it("starts a read with every column pending, and counts the answered ones", _ {
		PeakRead read;
		expect(read.pending()).to_be_true();
		expect(read.answered()).to_equal(0);
		expect(PeakRead::all_past_end().pending()).to_be_false();
		expect(PeakRead::all_past_end().answered()).to_equal(16);
		read.set(3, ColumnRead::of({.min = 1, .max = 2}));
		read.set(4, ColumnRead::past_end());
		expect(read.answered()).to_equal(2);
		expect(read.column(3).state()).to_equal(ColumnState::PEAK);
		expect(read.column(4).state()).to_equal(ColumnState::PAST_END);
		expect(read.column(5).state()).to_equal(ColumnState::PENDING);
	});
	it("sees pending columns only inside the range it is asked about", _ {
		PeakRead read = PeakRead::all_past_end();
		read.set(10, ColumnRead::pending());
		expect(read.pending(0, 10)).to_be_false();
		expect(read.pending(10, 11)).to_be_true();
		expect(read.pending(11, 16)).to_be_false();
		expect(read.pending()).to_be_true();
	});
	it("gives a column's bar only where it holds a peak", _ {
		PeakRead read;
		const Peak p{.min = -(1 << 22), .max = 1 << 22};
		read.set(2, ColumnRead::of(p));
		read.set(3, ColumnRead::past_end());
		expect(read.bar(2, 0, 1 << 20).value()).to_equal(bar_of(p, 0, 1 << 20));
		expect(read.bar(3, 0, 1 << 20)).not_().to_have_value();
		expect(read.bar(4, 0, 1 << 20)).not_().to_have_value();
	});
	it("Q14: draws a full-scale column at full height, saturating instead of wrapping", _ {
		const Peak p{.min = 0, .max = kI32Max};
		const Bar b = bar_of(p, -1, (1 << 28) - 1);
		expect(b.top).to_be_between((8 << 24) - 1, (8 << 24) + 1);
		expect(b.bottom).to_be_between(-1, 1);
		// The oracle's 32-bit subtraction wraps the top below the centre.
		expect(oracle_bar(p, -1, (1 << 28) - 1).top).to_be_less_than(0);
		const Peak far{.min = 0, .max = 1 << 29};
		expect(bar_of(far, 0, 1 << 20).top).to_equal(kI32Max);
		expect(oracle_bar(far, 0, 1 << 20).top).not_().to_equal(kI32Max);
	});
	it("Q15: draws the one-pad bar at the centre for a span of 0 or less", _ {
		const Bar centred{.bottom = -(1 << 23), .top = 1 << 23};
		for (const int32_t low : kValues) {
			for (const int32_t high : kValues) {
				if (low <= high) {
					expect(bar_of({.min = low, .max = high}, 0, 0)).to_equal(centred);
					expect(bar_of({.min = low, .max = high}, 7, -1)).to_equal(centred);
				}
			}
		}
		// At -1 the oracle negates each end, past the int32_t range, and the narrowing wraps them to a bar 48 pads
		// tall; at 0 it divides by zero, so there is no oracle there.
		expect(oracle_bar({.min = -1000, .max = 1000}, 0, -1)).to_equal(Bar{.bottom = -402653184, .top = 402653184});
	});
	it("Q16: measures the most negative value, clamps the ratio before narrowing, and draws a silent waveform dark", _ {
		expect(single_row_brightness({.min = kI32Min, .max = 0}, kI32Max)).to_equal(256);
		// No oracle for the most negative value: its std::abs is undefined.
		expect(single_row_brightness({.min = 0, .max = 1 << 30}, 1)).to_equal(256);
		expect(oracle_brightness({.min = 0, .max = 1 << 30}, 1)).not_().to_equal(256);
		expect(single_row_brightness({.min = -3, .max = 3}, 0)).to_equal(0);
	});
	it("Q21 (kept): centres a too-thin bar on the oracle's midpoint", _ {
		const Peak p{.min = 100, .max = 200};
		expect(bar_of(p, 0, 1 << 20)).to_equal(oracle_bar(p, 0, 1 << 20));
		const Peak q{.min = -(1 << 19), .max = (1 << 19) - 3};
		expect(bar_of(q, 11, 1 << 22)).to_equal(oracle_bar(q, 11, 1 << 22));
	});
});
// clang-format on

DELUGE_SPEC(peaks_spec, peaks_group)
