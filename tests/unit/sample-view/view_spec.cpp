// view.h against the legacy navigator's transcription: every open, clamp, scroll and zoom agrees with the oracle
// outside the Fixed-quirks rows, and each row's cases pin the correct value.
#include "sample-view/view.h"

#include "fixed_quirks.h"
#include "legacy_oracle.h"
#include "nav_cases.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::sample_view;
using namespace deluge::sample_view::test;

namespace {

constexpr int64_t kI32Max = std::numeric_limits<int32_t>::max();

/// @brief The view @p start names, made by the operations that reach it.
WaveformView view_of(const NavStart& start) {
	return start.view;
}

oracle::Nav oracle_of(const NavStart& start) {
	return {.length = start.length, .xZoom = start.zoom, .xScroll = start.scroll};
}

/// @brief The oracle's marker columns for @p pins: an unset pin is legacy's invalid column.
std::array<oracle::MarkerColumn, 4> oracle_cols(const std::array<MarkerPin, 4>& pins) {
	std::array<oracle::MarkerColumn, 4> cols{};
	for (size_t m = 0; m < 4; m++) {
		cols[m] = {.pos = static_cast<int32_t>(pins[m].position),
		           .colOnScreen = pins[m].column ? static_cast<int32_t>(*pins[m].column) : oracle::kInvalidColumn};
	}
	return cols;
}

/// @brief The pins of a zone at @p v, by column: none for an unset one.
std::array<MarkerPin, 4> pins_at(const WaveformView& v, std::array<std::optional<int64_t>, 4> columns) {
	const NavStart s{.length = v.length(), .scroll = v.scroll(), .zoom = v.zoom()};
	std::array<MarkerPin, 4> pins{};
	for (size_t m = 0; m < 4; m++) {
		pins[m] = nav_pin(s, m, columns[m]);
	}
	return pins;
}

} // namespace

// clang-format off
describe view_group("view.h", ${
	it("opens every remembered view as the oracle does outside the listed rows", _ {
		Skips s;
		for (const NavOpen& o : nav_opens()) {
			if (const auto q = open_quirk(o)) {
				s.skip(*q);
				continue;
			}
			s.compared++;
			const WaveformView v = WaveformView::open(
			    o.length, o.zoom == 0 ? std::nullopt : std::optional{Window{.scroll = o.scroll, .zoom = o.zoom}});
			oracle::Nav n{.length = o.length};
			n.opened(o.scroll, o.zoom);
			if ((v.scroll() != n.xScroll || v.zoom() != n.xZoom) && s.fail()) {
				std::cout << "open " << o.length << " {" << o.scroll << ", " << o.zoom << "}: {" << v.scroll() << ", "
				          << v.zoom() << "} against the oracle's {" << n.xScroll << ", " << n.xZoom << "}\n";
			}
		}
		s.print("open");
		expect(s.failures).to_equal(0);
		for (const Quirk q : {Quirk::Q4, Quirk::Q9, Quirk::Q10, Quirk::Q13}) {
			expect(s.of(q)).to_be_greater_than(0);
		}
	});
	it("gives every view's whole zoom, zoomed-in test, clamp and move as the oracle does outside the listed rows", _ {
		Skips s;
		for (const NavStart& start : nav_starts()) {
			const WaveformView v = view_of(start);
			expect(v.scroll()).to_equal(start.scroll);
			expect(v.zoom()).to_equal(start.zoom);
			if (const auto q = state_quirk(start)) {
				s.skip(*q);
			}
			else {
				s.compared++;
				const oracle::Nav n = oracle_of(start);
				if ((v.max_zoom() != n.max_zoom() || v.zoomed_in() != n.is_zoomed_in()) && s.fail()) {
					std::cout << "state " << start.length << " {" << start.scroll << ", " << start.zoom << "}\n";
				}
			}
			for (const bool extra : {false, true}) {
				if (const auto q = clamp_quirk(start.length, start.scroll, start.zoom, extra)) {
					s.skip(*q);
					continue;
				}
				s.compared++;
				oracle::Nav n = oracle_of(start);
				n.adjust_scroll_position(extra);
				const WaveformView c = v.clamped(extra);
				if ((c.scroll() != n.xScroll || c.zoom() != n.xZoom) && s.fail()) {
					std::cout << "clamp " << start.length << " {" << start.scroll << ", " << start.zoom << "} extra "
					          << extra << ": " << c.scroll() << " against the oracle's " << n.xScroll << "\n";
				}
			}
			for (const int32_t columns : {-3, -1, 1, 3}) {
				s.compared++;
				int64_t scroll = start.scroll + start.zoom * columns;
				scroll = scroll < 0 ? 0 : scroll;
				if (v.moved(columns).scroll() != scroll && s.fail()) {
					std::cout << "move " << columns << " from " << start.scroll << "\n";
				}
			}
		}
		s.print("state");
		expect(s.failures).to_equal(0);
		for (const Quirk q : {Quirk::Q4, Quirk::Q9, Quirk::Q10}) {
			expect(s.of(q)).to_be_greater_than(0);
		}
	});
	it("scrolls every view as the oracle does outside the listed rows", _ {
		Skips s;
		// No end marker shown; an unset end; an end on screen; an end off screen to the right.
		const std::array<std::optional<int32_t>, 4> ends{std::nullopt, oracle::kInvalidColumn, 15, 16};
		for (const NavStart& start : nav_starts()) {
			const WaveformView v = view_of(start);
			for (const int32_t offset : {-1, 1}) {
				for (const bool extra : {false, true}) {
					if (const auto q = scroll_quirk(start, offset, extra)) {
						s.skip(*q);
						continue;
					}
					for (size_t e = 0; e < ends.size(); e++) {
						s.compared++;
						std::array<oracle::MarkerColumn, 4> cols{};
						for (auto& c : cols) {
							c = {.pos = 0, .colOnScreen = oracle::kInvalidColumn};
						}
						if (ends[e]) {
							cols[oracle::kMarkerEnd].colOnScreen = *ends[e];
						}
						const std::optional<int64_t> end_column =
						    (ends[e] && *ends[e] != oracle::kInvalidColumn) ? std::optional<int64_t>{*ends[e]}
						                                                    : std::nullopt;
						oracle::Nav n = oracle_of(start);
						const bool moved = n.scroll(offset, extra, ends[e] ? cols.data() : nullptr);
						const std::optional<WaveformView> r = v.scrolled(offset, extra, end_column);
						const bool same = moved == r.has_value() && (!r || (r->scroll() == n.xScroll && r->zoom() == n.xZoom));
						if (!same && s.fail()) {
							std::cout << "scroll " << offset << " extra " << extra << " end " << e << " from "
							          << start.length << " {" << start.scroll << ", " << start.zoom << "}: "
							          << (r ? r->scroll() : -999) << " against the oracle's "
							          << (moved ? n.xScroll : -999) << "\n";
						}
					}
				}
			}
		}
		s.print("scroll");
		expect(s.failures).to_equal(0);
		for (const Quirk q : {Quirk::Q11, Quirk::Q12}) {
			expect(s.of(q)).to_be_greater_than(0);
		}
	});
	it("zooms every view as the oracle does outside the listed rows", _ {
		Skips s;
		for_each_zoom_case([&](const NavCase& c) {
			if (const auto q = zoom_quirk(c)) {
				s.skip(*q);
				return;
			}
			s.compared++;
			const WaveformView& v = c.view;
			const std::optional<WaveformView> r = v.zoomed(c.offset, c.pins, c.preferred, c.extra_right);
			oracle::Nav n = oracle_of({.length = c.length, .scroll = c.scroll, .zoom = c.zoom});
			const std::array<oracle::MarkerColumn, 4> cols = oracle_cols(c.pins);
			const bool moved = n.zoom(c.offset, c.extra_right, c.preferred ? cols.data() : nullptr,
			                          c.preferred ? static_cast<int32_t>(*c.preferred) : oracle::kMarkerNone);
			const bool same = moved == r.has_value() && (!r || (r->scroll() == n.xScroll && r->zoom() == n.xZoom));
			if (!same && s.fail()) {
				std::cout << "zoom " << c.offset << " extra " << c.extra_right << " preferred "
				          << (c.preferred ? static_cast<int64_t>(*c.preferred) : -1) << " from " << c.length << " {"
				          << c.scroll << ", " << c.zoom << "}: {" << (r ? r->scroll() : -999) << ", "
				          << (r ? r->zoom() : -999) << "} against the oracle's {" << (moved ? n.xScroll : -999) << ", "
				          << (moved ? n.xZoom : -999) << "}\n";
			}
		});
		s.print("zoom");
		expect(s.failures).to_equal(0);
		for (const Quirk q : {Quirk::Q6, Quirk::Q7, Quirk::Q8, Quirk::Q9, Quirk::Q10}) {
			expect(s.of(q)).to_be_greater_than(0);
		}
	});
	it("Q4: gives an empty waveform a whole zoom of 1, and neither zoom direction moves it", _ {
		const WaveformView empty = WaveformView::open(0, std::nullopt);
		expect(empty.max_zoom()).to_equal(1);
		expect(oracle::Nav{.length = 0}.max_zoom()).to_equal(0);
		expect(empty.scroll()).to_equal(0);
		expect(empty.zoom()).to_equal(1);
		expect(empty.zoomed(1, {}, std::nullopt, false)).not_().to_have_value();
		expect(empty.zoomed(-1, {}, std::nullopt, false)).not_().to_have_value();
		expect(WaveformView::open((uint64_t{1} << 36) + 1, std::nullopt).max_zoom()).to_equal((int64_t{1} << 32) + 1);
	});
	it("Q5: steps the zoom in 64 bits past 2^30", _ {
		const uint64_t long_file = uint64_t{1} << 40;
		const auto in = [&](int64_t zoom) {
			return WaveformView::open(long_file, Window{.scroll = 0, .zoom = zoom}).zoomed(1, {}, std::nullopt, false);
		};
		expect(in((int64_t{1} << 31) + 1).value().zoom()).to_equal(int64_t{1} << 30);
		expect(in((int64_t{1} << 31) - 1).value().zoom()).to_equal(int64_t{1} << 30);
		const auto out = WaveformView::open(long_file, Window{.scroll = 0, .zoom = int64_t{1} << 32})
		                     .zoomed(-1, {}, std::nullopt, false);
		expect(out.value().zoom()).to_equal(int64_t{1} << 33);
		// Zooming out, the oracle's 32-bit limit narrows 2^36 to 0, so it refuses. Zooming in it has no answer: its
		// power-of-two search stops below 2^31 and reads an uninitialised value.
		oracle::Nav n{.length = long_file, .xZoom = int64_t{1} << 32};
		expect(n.zoom(-1, false, nullptr, oracle::kMarkerNone)).to_be_false();
	});
	it("Q6: keeps a centre-pinned frame past 2^31 where it was", _ {
		const int64_t zoom = int64_t{1} << 20;
		const WaveformView v = WaveformView::open(uint64_t{1} << 33, Window{.scroll = int64_t{1} << 31, .zoom = zoom});
		const WaveformView r = v.zoomed(1, {}, std::nullopt, false).value();
		const int64_t pinned = (int64_t{1} << 31) + 8 * zoom;
		expect(r.scroll() + 8 * r.zoom()).to_equal(pinned);
		oracle::Nav n{.length = uint64_t{1} << 33, .xZoom = zoom, .xScroll = int64_t{1} << 31};
		n.zoom(1, false, nullptr, oracle::kMarkerNone);
		expect(n.xScroll + 8 * n.xZoom).not_().to_equal(pinned);
	});
	it("Q7: measures every competing pin by its pin point", _ {
		const WaveformView v = WaveformView::open(1 << 20, Window{.scroll = 1 << 12, .zoom = 256});
		// The preferred pin (the end) off screen; the start at column 9, the loop end at column 7 (pin point 8).
		const std::array<MarkerPin, 4> pins = pins_at(v, {9, std::nullopt, 7, 20});
		const WaveformView r = v.zoomed(1, pins, 3, false).value();
		expect(r.scroll() + 8 * r.zoom()).to_equal(pins[2].position);
		oracle::Nav n{.length = 1 << 20, .xZoom = 256, .xScroll = 1 << 12};
		const auto cols = oracle_cols(pins);
		n.zoom(1, false, cols.data(), 3);
		expect(n.xScroll + 9 * n.xZoom).to_equal(pins[0].position);
	});
	it("Q8: rounds a right-edge pin's scroll up with a true ceiling, never past the start", _ {
		// The end at column 0 (pin point 1), zoom 8 out to 16: the unaligned scroll is 8 - 16 = -8.
		const WaveformView v = WaveformView::open(1 << 20, Window{.scroll = 0, .zoom = 8});
		const std::array<MarkerPin, 4> pins = pins_at(v, {std::nullopt, std::nullopt, std::nullopt, 0});
		expect(v.zoomed(-1, pins, 3, false).value().scroll()).to_equal(0);
		oracle::Nav n{.length = 1 << 20, .xZoom = 8, .xScroll = 0};
		const auto cols = oracle_cols(pins);
		n.zoom(-1, false, cols.data(), 3);
		expect(n.xScroll).to_equal(16);
		// Unaligned at -2 at zoom 4: the end at column 0 of a zoom-2 view, zoomed out to 4.
		const WaveformView w = WaveformView::open(1 << 20, Window{.scroll = 0, .zoom = 2});
		const std::array<MarkerPin, 4> wp = pins_at(w, {std::nullopt, std::nullopt, std::nullopt, 0});
		expect(w.zoomed(-1, wp, 3, false).value().scroll()).to_equal(0);
		oracle::Nav m{.length = 1 << 20, .xZoom = 2, .xScroll = 0};
		const auto wcols = oracle_cols(wp);
		m.zoom(-1, false, wcols.data(), 3);
		expect(m.xScroll).to_equal(4);
		// Unaligned at exactly 0 at zoom 8: the end at column 1 of a zoom-4 view at scroll 8 (frame 16, pin point 2),
		// zoomed out to 8. The oracle's (0 - 1) / 8 truncates to 0 and rounds up a whole column.
		const WaveformView z = WaveformView::open(1 << 20, Window{.scroll = 8, .zoom = 4});
		const std::array<MarkerPin, 4> zp = pins_at(z, {std::nullopt, std::nullopt, std::nullopt, 1});
		expect(zp[3].position).to_equal(16);
		const WaveformView zr = z.zoomed(-1, zp, 3, false).value();
		expect(zr.zoom()).to_equal(8);
		expect(zr.scroll()).to_equal(0);
		oracle::Nav o{.length = 1 << 20, .xZoom = 4, .xScroll = 8};
		const auto zcols = oracle_cols(zp);
		o.zoom(-1, false, zcols.data(), 3);
		expect(o.xScroll).to_equal(8);
	});
	it("Q9: clamps past 2^32 frames in 64 bits", _ {
		const uint64_t length = (uint64_t{1} << 32) + 16;
		const WaveformView v = WaveformView::open(length, Window{.scroll = int64_t{1} << 32, .zoom = 1});
		expect(v.clamped(false).scroll()).to_equal(int64_t{1} << 32);
		oracle::Nav n{.length = length, .xZoom = 1, .xScroll = int64_t{1} << 32};
		n.adjust_scroll_position(false);
		expect(n.xScroll).not_().to_equal(int64_t{1} << 32);
	});
	it("Q10: starts a waveform narrower than the view at column 0", _ {
		const WaveformView v = WaveformView::open(17, Window{.scroll = 0, .zoom = 2});
		expect(v.scroll()).to_equal(0);
		expect(v.zoom()).to_equal(2);
		expect(v.clamped(false).scroll()).to_equal(0);
		oracle::Nav n{.length = 17, .xZoom = 2, .xScroll = 0};
		n.adjust_scroll_position(false);
		expect(n.xScroll).to_equal(-14);
	});
	it("Q11: scrolls past 2^31 with extra scroll right, refusing only at the end of 64 bits", _ {
		const uint64_t length = uint64_t{1} << 33;
		for (const int64_t scroll : {kI32Max + 1 - 16, kI32Max + 1}) {
			const WaveformView v = WaveformView::open(length, Window{.scroll = scroll, .zoom = 1});
			expect(v.scrolled(1, true, std::nullopt).value().scroll()).to_equal(scroll + 1);
			oracle::Nav n{.length = length, .xZoom = 1, .xScroll = scroll};
			expect(n.scroll(1, true, nullptr)).to_be_false();
		}
		const WaveformView big = WaveformView::open(uint64_t{1} << 36, Window{.scroll = int64_t{1} << 30,
		                                                                      .zoom = int64_t{1} << 28});
		expect(big.scrolled(1, true, std::nullopt)).to_have_value();
		oracle::Nav nb{.length = uint64_t{1} << 36, .xZoom = int64_t{1} << 28, .xScroll = int64_t{1} << 30};
		expect(nb.scroll(1, true, nullptr)).to_be_true();
		// At zoom 2^58 the view moves right until its end, one past its last frame, would not fit in an int64_t:
		// fifteen columns on, at 15 * 2^58, the next view's end would be 2^63.
		const int64_t huge = int64_t{1} << 58;
		std::optional<WaveformView> edge = WaveformView::open(uint64_t{1} << 62, Window{.scroll = 0, .zoom = huge});
		int32_t moves = 0;
		for (std::optional<WaveformView> next = edge->scrolled(1, true, std::nullopt); next && moves < 64;
		     next = edge->scrolled(1, true, std::nullopt)) {
			edge = next;
			moves++;
		}
		expect(moves).to_equal(15);
		expect(edge->scroll()).to_equal(15 * huge);
	});
	it("Q11: moves across 2^31 at zoom 2^28 to its own frame", _ {
		// The refusal test passes (the wrapped sum is the scroll itself), but the oracle keeps the moved scroll in an
		// int32_t, so it lands at a wrapped negative frame.
		const int64_t zoom = int64_t{1} << 28;
		const WaveformView v = WaveformView::open(uint64_t{1} << 36, Window{.scroll = 7 * zoom, .zoom = zoom});
		expect(v.scroll()).to_equal(7 * zoom);
		expect(v.scrolled(1, true, std::nullopt).value().scroll()).to_equal(int64_t{1} << 31);
		oracle::Nav n{.length = uint64_t{1} << 36, .xZoom = zoom, .xScroll = 7 * zoom};
		expect(n.scroll(1, true, nullptr)).to_be_true();
		expect(n.xScroll).to_be_less_than(0);
	});
	it("Q12: stops a left scroll at the waveform's start", _ {
		const WaveformView v = WaveformView::open(1 << 20, Window{.scroll = 3, .zoom = 8});
		expect(v.scrolled(-1, false, std::nullopt).value().scroll()).to_equal(0);
		oracle::Nav n{.length = 1 << 20, .xZoom = 8, .xScroll = 3};
		n.scroll(-1, false, nullptr);
		expect(n.xScroll).to_equal(-8);
	});
	it("Q13: clamps a remembered zoom into the waveform's zooms", _ {
		const WaveformView v = WaveformView::open(100, Window{.scroll = 0, .zoom = 1000});
		expect(v.zoom()).to_equal(v.max_zoom());
		expect(v.max_zoom()).to_equal(7);
		expect(v.scroll()).to_equal(0);
		expect(v.zoomed_in()).to_be_false();
		oracle::Nav n{.length = 100};
		n.opened(0, 1000);
		expect(n.xZoom).to_equal(1000);
		expect(n.is_zoomed_in()).to_be_true();
		expect(WaveformView::open(100, Window{.scroll = 0, .zoom = -5}).zoom()).to_equal(1);
	});
	it("Q18 (kept): an on-screen preferred pin wins over a nearer one before it", _ {
		const WaveformView v = WaveformView::open(1 << 20, Window{.scroll = 1 << 12, .zoom = 256});
		const std::array<MarkerPin, 4> pins = pins_at(v, {8, 2, std::nullopt, std::nullopt});
		const WaveformView r = v.zoomed(1, pins, 1, false).value();
		oracle::Nav n{.length = 1 << 20, .xZoom = 256, .xScroll = 1 << 12};
		const auto cols = oracle_cols(pins);
		n.zoom(1, false, cols.data(), 1);
		expect(r.scroll()).to_equal(n.xScroll);
		expect(r.scroll() + 2 * r.zoom()).to_equal(pins[1].position);
	});
	it("Q19 (kept): refuses a right scroll at the end with no end marker to reach", _ {
		const WaveformView v = WaveformView::open(1 << 10, std::nullopt);
		expect(v.scrolled(1, false, std::nullopt)).not_().to_have_value();
		std::array<oracle::MarkerColumn, 4> cols{};
		for (auto& c : cols) {
			c = {.pos = 0, .colOnScreen = oracle::kInvalidColumn};
		}
		oracle::Nav n{.length = 1 << 10, .xZoom = v.zoom(), .xScroll = 0};
		expect(n.scroll(1, false, cols.data())).to_be_false();
	});
	it("Q20 (kept): steps zoom 5 in to 2 and 6 in to 4", _ {
		const auto in = [](int64_t zoom) {
			return WaveformView::open(1 << 20, Window{.scroll = 0, .zoom = zoom}).zoomed(1, {}, std::nullopt, false);
		};
		expect(in(5).value().zoom()).to_equal(2);
		expect(in(6).value().zoom()).to_equal(4);
		oracle::Nav n5{.length = 1 << 20, .xZoom = 5, .xScroll = 0};
		n5.zoom(1, false, nullptr, oracle::kMarkerNone);
		expect(n5.xZoom).to_equal(2);
		oracle::Nav n6{.length = 1 << 20, .xZoom = 6, .xScroll = 0};
		n6.zoom(1, false, nullptr, oracle::kMarkerNone);
		expect(n6.xZoom).to_equal(4);
	});
});
// clang-format on

DELUGE_SPEC(view_spec, view_group)
