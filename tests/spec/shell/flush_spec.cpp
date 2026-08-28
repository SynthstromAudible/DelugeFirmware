#include "gfx/grid/tween.h"
#include "shell/shell.h"
#include "support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using deluge::notify::Dirty;

namespace {

/// The kind the one test scene registers under.
constexpr SceneKind kBase = SceneKind::FirstApp;

/// `LedId`'s values equal `ButtonId`'s for the LEDs under buttons (`shell/leds.h`'s ruling), and
/// the extended `uitest::test_profile_input`'s `led_by_button` (`fake_surfaces.h`) wires `Play` to
/// board LED 9 -- exactly the id/board-index pair this file's LED-diff spec needs.
constexpr deluge::ui::LedId kPlayLed = static_cast<deluge::ui::LedId>(static_cast<uint8_t>(ButtonId::Play));
constexpr uint8_t kPlayBoardLed = 9;

/// Legacy's byte budget for one main-grid column-pair send, independently of `surfaces.h`'s
/// `kBytesPerColumnPair`: `kNumBytesInColUpdateMessage = 49` and `kNumBytesInMainPadRedraw =
/// kNumBytesInColUpdateMessage * 8` (`src/definitions_cxx.hpp:94,100`) for the whole 8-pair main
/// grid, so one pair is that figure divided by 8.
constexpr int32_t kLegacyNumBytesInColUpdateMessage = 49;
constexpr int32_t kLegacyNumBytesInMainPadRedraw = kLegacyNumBytesInColUpdateMessage * 8;
static_assert(kBytesPerColumnPair == static_cast<uint32_t>(kLegacyNumBytesInMainPadRedraw / 8));

/// Legacy's full greyout strength (`setGreyoutAmount(1)` -> `greyProportion = 1 * 6500000`,
/// `hid/led/pad_leds.cpp:786-788`), repeated here (as `tick_spec.cpp` also does) so this file pins
/// the value the `GreyOut` tween is bound to rather than reusing whatever constant the
/// implementation happens to define.
constexpr int32_t kGreyoutAmountFull = 6500000;
/// Legacy's backpressure retry interval (`setTimerForSoon`, `hid/led/pad_leds.cpp:1063-1067`:
/// `uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 20)`), independently of `shell.cpp`'s own
/// (unexported) constant of the same value.
constexpr uint32_t kBackpressureRetryMs = 20;
/// Legacy's `UI_MS_PER_REFRESH` (`board_config.h:55`), the `Fade` tween's step cadence
/// (`renderFade`, `pad_leds.cpp:1479`).
constexpr uint32_t kFadeCadenceMs = 50;

/// @brief `count` distinct colours, deterministic from `seed`, for a scroll-tween spec: a flat
///        fill would look identical at any offset, so shifting it could never be told apart from
///        merely recolouring it.
std::vector<RGB> gradient(size_t count, uint8_t seed) {
	std::vector<RGB> v;
	v.reserve(count);
	for (size_t i = 0; i < count; i++) {
		const auto x = static_cast<uint8_t>(i);
		v.push_back(RGB{static_cast<uint8_t>(x * 7 + seed), static_cast<uint8_t>(x * 13 + seed),
		                static_cast<uint8_t>(x * 19 + seed)});
	}
	return v;
}

/// @brief A scene that paints the whole grid one flat colour (with a few columns overridable),
///        draws a trivial mark on every OLED role, and declares `kPlayLed` and the upper knob
///        indicator from public fields -- everything `Shell::flush`'s specs drive from outside.
class FlushScene final : public Scene {
public:
	SceneKind kind() const override { return kBase; }

	void render_oled(SurfaceRole, deluge::ui::oled::OledFrame& frame) override {
		oled_renders++;
		frame.hline(5, 0, 20);
	}

	void render_grid(deluge::ui::grid::GridFrame& frame) override {
		const deluge::gfx::grid::Rect b = frame.bounds();
		for (int32_t y = 0; y < b.h; y++) {
			for (int32_t x = 0; x < b.w; x++) {
				RGB c = colour;
				// A pattern (one colour per column, or per row) lets a scroll spec tell "shifted"
				// apart from "recoloured" -- a flat fill would look identical at any offset.
				if (!row_colours.empty() && static_cast<size_t>(y) < row_colours.size()) {
					c = row_colours[static_cast<size_t>(y)];
				}
				else if (!column_colours.empty() && static_cast<size_t>(x) < column_colours.size()) {
					c = column_colours[static_cast<size_t>(x)];
				}
				for (const auto& [col, tint] : column_overrides) {
					if (col == x) {
						c = tint;
					}
				}
				(void)frame.pad(x, y, c);
			}
		}
	}

	void leds(deluge::ui::LedFrame& frame) override {
		(void)frame.set(
		    kPlayLed,
		    deluge::ui::LedState{play_on ? deluge::ui::LedState::Kind::On : deluge::ui::LedState::Kind::Off, 0, 0});
		if (declare_indicator) {
			(void)frame.indicator(deluge::ui::IndicatorId::Upper, indicator);
		}
	}

	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

	deluge::ui::grid::GreyoutMask greyout_mask() const override { return mask; }

	RGB colour{10, 20, 30};
	std::vector<std::pair<int32_t, RGB>> column_overrides;
	std::vector<RGB> column_colours;
	std::vector<RGB> row_colours;
	deluge::ui::grid::GreyoutMask mask{};
	bool play_on = true;
	bool declare_indicator = true;
	deluge::ui::KnobIndicator indicator{{1, 2, 3, 4}};
	int oled_renders = 0;
};

std::array<Scene*, 18> table_for(Scene& base) {
	std::array<Scene*, 18> t{};
	t[static_cast<size_t>(kBase)] = &base;
	return t;
}

/// @brief `kBoard2`'s two-OLED profile, on top of `test_profile_input`'s (now LED-mapped) button
///        matrix.
BoardProfile flush_profile() {
	static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main, SurfaceRole::Aux};
	static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle48, &deluge::ui::kDelugeStyle64};
	BoardProfile p = uitest::test_profile_input();
	p.oled_role_by_index = kRoles;
	p.style_by_index = kStyles;
	return p;
}

struct Rig {
	BoardProfile profile = flush_profile();
	FlushScene base;
	std::array<Scene*, 18> table = table_for(base);
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	Shell shell{fake, profile, reg, Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	Rig() = default;
};

/// One recorded call to the mirror sink under test.
struct MirrorCall {
	size_t byte_count;
	uint16_t w, h;
};
/// `MirrorSink` is a plain function pointer and cannot capture, so the sink under test forwards to
/// whichever vector this points at; each mirror spec points it at a local recorder for the
/// duration of the test and clears it before returning.
std::vector<MirrorCall>* g_mirror_target = nullptr;
void record_mirror(std::span<const uint8_t> bytes, uint16_t w, uint16_t h) {
	if (g_mirror_target != nullptr) {
		g_mirror_target->push_back(MirrorCall{bytes.size(), w, h});
	}
}

} // namespace

// clang-format off
describe shell_flush("shell::Shell::flush", $ {
	it("blits only surfaces that rendered, skips busy panels and retries next tick", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.fake.busy[0] = true;

		r.shell.tick(0);
		// Both surfaces rendered (first tick), but panel 0 is busy: only panel 1 goes out, and
		// panel 0's `blit_pending` stays armed rather than being dropped.
		expect(r.base.oled_renders).to_equal(2);
		expect(r.fake.blits.size()).to_equal(size_t(1));
		expect(r.fake.blits[0].index).to_equal(uint8_t(1));
		expect(r.fake.oled()[0].blit_pending).to_be_true();
		expect(r.fake.oled()[1].blit_pending).to_be_false();

		r.fake.busy[0] = false;
		r.shell.tick(10);
		// Nothing dirty and no input, so neither surface renders again -- yet panel 0's leftover
		// pending blit still goes out now that its panel is free.
		expect(r.base.oled_renders).to_equal(2);
		expect(r.fake.blits.size()).to_equal(size_t(2));
		expect(r.fake.blits[1].index).to_equal(uint8_t(0));
		expect(r.fake.oled()[0].blit_pending).to_be_false();
	});

	it("sends only changed column pairs in order and honours backpressure", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		// First frame: every one of `kBoard2`'s 9 column pairs (18 total columns) is new.
		expect(r.fake.columns_sent.size()).to_equal(size_t(9));
		expect(r.fake.grid().dirty).to_be_false();

		// Column 3 sits in pair 1 (columns 2,3); column 12 in pair 6 (columns 12,13).
		r.base.column_overrides = {{3, RGB{9, 9, 9}}, {12, RGB{7, 7, 7}}};
		r.fake.space = kBytesPerColumnPair; // budget for exactly one pair this tick.
		deluge::notify::dirty().raise(Dirty::GridMain);
		r.shell.tick(10);

		expect(r.fake.columns_sent.size()).to_equal(size_t(10)); // the 9 plus this tick's one pair
		expect(r.fake.columns_sent.back().first).to_equal(uint8_t(1));
		expect(r.fake.grid().dirty).to_be_true(); // pair 6 is still owed
		// Intra-pair byte order: column `pair * 2` (2, untouched -- the flat `colour`) rows 0..7,
		// then column `pair * 2 + 1` (3, overridden) rows 0..7 -- `sortLedsForCol`'s order
		// (`pad_leds.cpp:265-272`), which `read_column_pair` ports.
		const ColumnPair& pair1 = r.fake.columns_sent.back().second;
		for (uint8_t i = 0; i < 8; i++) {
			expect(pair1.colours[i] == r.base.colour).to_be_true();
			expect(pair1.colours[i + 8] == RGB{9, 9, 9}).to_be_true();
		}

		// The retry, with no dirty fact raised again: the backpressure branch re-armed the gate.
		r.shell.tick(20);
		expect(r.fake.columns_sent.size()).to_equal(size_t(11));
		expect(r.fake.columns_sent.back().first).to_equal(uint8_t(6));
		expect(r.fake.grid().dirty).to_be_false();
		const ColumnPair& pair6 = r.fake.columns_sent.back().second;
		for (uint8_t i = 0; i < 8; i++) {
			expect(pair6.colours[i] == RGB{7, 7, 7}).to_be_true();
			expect(pair6.colours[i + 8] == r.base.colour).to_be_true();
		}
	});

	it("LED diff emits only changes; indicators only on change", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});

		r.shell.tick(0);
		expect(r.fake.led_writes.size()).to_equal(size_t(1));
		expect(r.fake.led_writes[0].first).to_equal(kPlayBoardLed);
		expect(r.fake.led_writes[0].second).to_be_true();
		expect(r.fake.indicator_writes.size()).to_equal(size_t(1));

		// `derive_leds` recomposes every tick regardless of dirtiness, but nothing changed, so
		// `flush` has nothing new to send.
		r.shell.tick(10);
		expect(r.fake.led_writes.size()).to_equal(size_t(1));
		expect(r.fake.indicator_writes.size()).to_equal(size_t(1));

		r.base.play_on = false;
		r.shell.tick(20);
		expect(r.fake.led_writes.size()).to_equal(size_t(2));
		expect(r.fake.led_writes[1].first).to_equal(kPlayBoardLed);
		expect(r.fake.led_writes[1].second).to_be_false();
		expect(r.fake.indicator_writes.size()).to_equal(size_t(1)); // unchanged
	});

	it("mirror sees the main surface after a blit, throttled, and never drops a deferred frame", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		std::vector<MirrorCall> calls;
		g_mirror_target = &calls;
		r.shell.set_mirror(&record_mirror);

		r.shell.tick(0);
		expect(calls.size()).to_equal(size_t(1));
		expect(calls[0].w).to_equal(uint16_t(128));
		expect(calls[0].h).to_equal(uint16_t(48));

		// A reblit 10 ms later, inside the 100 ms throttle window, is not sent immediately...
		deluge::notify::dirty().raise(Dirty::Oled);
		r.shell.tick(10);
		expect(calls.size()).to_equal(size_t(1));
		// ...but is remembered, not dropped: `next_wake()` reports exactly when it is owed.
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(100));

		const size_t blits_before_retry = r.fake.blits.size();
		// Nothing rendered this tick (no dirty fact, no input) -- yet once the interval has
		// elapsed, `Shell::flush` delivers the deferred frame on its own, with no new blit.
		r.shell.tick(100);
		expect(r.fake.blits.size()).to_equal(blits_before_retry);
		expect(calls.size()).to_equal(size_t(2));

		g_mirror_target = nullptr;
	});

	it("a fade tween interpolates from the snapshot to the live grid over its duration", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		const RGB kFrom{40, 60, 80};
		const RGB kTo{200, 10, 250};
		r.base.colour = kFrom;
		r.shell.tick(0);

		// Snapshot "from" is the grid exactly as it stands right now (`kFrom` everywhere); then
		// the scene starts painting `kTo` instead, which is what the next tick renders as "to".
		r.shell.start_tween(ActiveTween{ActiveTween::Kind::Fade, 0, 200, 0});
		r.base.colour = kTo;
		r.shell.tick(100);

		// Reproduce the same blend by hand, from two grids of matching dims.
		const deluge::gfx::grid::GridDimensions dims = r.fake.grid().grid().dims();
		deluge::gfx::grid::PadGrid from_grid(dims), to_grid(dims), expected(dims);
		for (uint8_t y = 0; y < dims.rows; y++) {
			for (uint8_t x = 0; x < dims.cols(); x++) {
				from_grid.set(x, y, kFrom);
				to_grid.set(x, y, kTo);
			}
		}
		deluge::gfx::grid::fade(from_grid, to_grid, 32768, expected);
		const deluge::gfx::grid::PadGrid& live = r.fake.grid().grid();
		expect(live.at(0, 0) == expected.at(0, 0)).to_be_true();
		expect(live.at(17, 7) == expected.at(17, 7)).to_be_true();
		// Halfway (t == 32768/65536), the blend must sit strictly between the two ends.
		expect(live.at(0, 0) == kFrom).to_be_false();
		expect(live.at(0, 0) == kTo).to_be_false();

		// Past the duration, the tween clears and the live grid shows through unblended.
		r.shell.tick(250);
		expect(r.fake.grid().grid().at(0, 0) == kTo).to_be_true();
	});

	it("a scroll-horizontal tween interpolates from the snapshot to the live grid over its duration", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		const deluge::gfx::grid::GridDimensions dims = r.fake.grid().grid().dims();
		const std::vector<RGB> from_cols = gradient(dims.cols(), 10);
		const std::vector<RGB> to_cols = gradient(dims.cols(), 200);
		r.base.column_colours = from_cols;
		r.shell.tick(0);

		// Snapshot "from" is `from_cols`; the scene then switches to `to_cols`, which the next
		// tick renders as "to".
		constexpr int32_t kTargetOffset = 4;
		r.shell.start_tween(ActiveTween{ActiveTween::Kind::ScrollH, 0, 200, kTargetOffset});
		r.base.column_colours = to_cols;
		r.shell.tick(100); // halfway: t == 32768, offset == kTargetOffset * t / 65536 == 2

		// Reproduce `horizontal::renderScroll`'s shape by hand, via the same `gfx::grid`
		// primitive `render_grid` calls, from two grids built out of the same column patterns.
		deluge::gfx::grid::PadGrid from_grid(dims), to_grid(dims), expected(dims);
		for (uint8_t y = 0; y < dims.rows; y++) {
			for (uint8_t x = 0; x < dims.cols(); x++) {
				from_grid.set(x, y, from_cols[x]);
				to_grid.set(x, y, to_cols[x]);
			}
		}
		deluge::gfx::grid::scroll_horizontal(from_grid, to_grid, 2, expected);
		const deluge::gfx::grid::PadGrid& live = r.fake.grid().grid();
		for (uint8_t y = 0; y < dims.rows; y++) {
			for (uint8_t x = 0; x < dims.cols(); x++) {
				expect(live.at(x, y) == expected.at(x, y)).to_be_true();
			}
		}

		// Past the duration, the tween clears and the live grid (`to_cols`) shows through
		// unblended -- equivalent to the scroll having completed.
		r.shell.tick(250);
		for (uint8_t x = 0; x < dims.cols(); x++) {
			expect(r.fake.grid().grid().at(x, 0) == to_cols[x]).to_be_true();
		}
	});

	it("a scroll-vertical tween interpolates from the snapshot to the live grid over its duration", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		const deluge::gfx::grid::GridDimensions dims = r.fake.grid().grid().dims();
		const std::vector<RGB> from_rows = gradient(dims.rows, 20);
		const std::vector<RGB> to_rows = gradient(dims.rows, 210);
		r.base.row_colours = from_rows;
		r.shell.tick(0);

		constexpr int32_t kTargetOffset = 2;
		r.shell.start_tween(ActiveTween{ActiveTween::Kind::ScrollV, 0, 200, kTargetOffset});
		r.base.row_colours = to_rows;
		r.shell.tick(100); // halfway: offset == 1

		// `vertical::renderScroll`'s shape (`hid/led/pad_leds.cpp:1431-1456`): the sidebar moves
		// with the rows, unlike the horizontal case -- `scroll_vertical` shifts every column.
		deluge::gfx::grid::PadGrid from_grid(dims), to_grid(dims), expected(dims);
		for (uint8_t y = 0; y < dims.rows; y++) {
			for (uint8_t x = 0; x < dims.cols(); x++) {
				from_grid.set(x, y, from_rows[y]);
				to_grid.set(x, y, to_rows[y]);
			}
		}
		deluge::gfx::grid::scroll_vertical(from_grid, to_grid, 1, expected);
		const deluge::gfx::grid::PadGrid& live = r.fake.grid().grid();
		for (uint8_t y = 0; y < dims.rows; y++) {
			for (uint8_t x = 0; x < dims.cols(); x++) {
				expect(live.at(x, y) == expected.at(x, y)).to_be_true();
			}
		}

		r.shell.tick(250); // past the duration: settles on the live (`to_rows`) grid, unblended
		for (uint8_t y = 0; y < dims.rows; y++) {
			expect(r.fake.grid().grid().at(0, y) == to_rows[y]).to_be_true();
		}
	});

	it("a greyout tween ramps the mask from half to full over its duration, replacing the static pass", _ {
		deluge::notify::dirty().drain();
		Rig r;
		// Grey every row (and therefore every pad, regardless of column) -- sidesteps
		// `GreyoutMask`'s right-to-left column numbering (see `tick_spec.cpp`'s greyout test).
		r.base.mask = deluge::ui::grid::GreyoutMask{.cols = 0, .rows = 0xFF};
		r.shell.init({kBase, 0});
		RGB colour{120, 90, 60};
		r.base.colour = colour;
		r.shell.tick(0);
		// Baseline: no tween is running, so the mask being in place (recomputed on this first,
		// stack-changing tick) means the ordinary static pass already grades it at full strength.
		const RGB full_grey = colour.greyOut(kGreyoutAmountFull);
		expect(r.fake.grid().grid().at(0, 0) == full_grey).to_be_true();

		r.shell.start_tween(ActiveTween{ActiveTween::Kind::GreyOut, 0, 200, kGreyoutAmountFull});
		r.shell.tick(100); // halfway: t == 32768, proportion == kGreyoutAmountFull / 2

		const RGB half_grey = colour.greyOut(kGreyoutAmountFull / 2);
		expect(r.fake.grid().grid().at(0, 0) == half_grey).to_be_true();
		// Not the static full-strength pass stacked underneath the ramp: if it were, this pad
		// would already be at `full_grey` regardless of the ramp's own halfway proportion.
		expect(r.fake.grid().grid().at(0, 0) == full_grey).to_be_false();

		// Past the duration, the ramp settles back at full strength -- legacy's `amountDone > 1`
		// snap (`pad_leds.cpp:984-987`), matching the ordinary static pass that resumes right after.
		r.shell.tick(250);
		expect(r.fake.grid().grid().at(0, 0) == full_grey).to_be_true();
	});

	it("next_wake sees a running tween's step cadence", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.shell.start_tween(ActiveTween{ActiveTween::Kind::Fade, 20, 200, 0});
		// Armed by `start_tween` itself, without needing an intervening tick: `request_repaint()`
		// alone (the old behaviour) would leave `next_wake()` at `std::nullopt`.
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(20 + kFadeCadenceMs));

		// A tick before that deadline changes nothing -- the gate is not yet due, so `render_grid`
		// does not even run and the original deadline stands.
		r.shell.tick(30);
		expect(*r.shell.next_wake()).to_equal(uint32_t(20 + kFadeCadenceMs));

		// Right at the deadline (elapsed 50 of 200 ms, still mid-tween), the render step re-arms
		// the same cadence from *this* tick's own clock, not the tween's start.
		r.shell.tick(20 + kFadeCadenceMs);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(20 + kFadeCadenceMs + kFadeCadenceMs));
	});

	it("next_wake sees the backpressure backlog's retry deadline, and the next tick past it drains more", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0); // baseline: all 9 pairs sent

		r.base.column_overrides = {{3, RGB{9, 9, 9}}, {12, RGB{7, 7, 7}}};
		r.fake.space = kBytesPerColumnPair; // budget for exactly one pair
		deluge::notify::dirty().raise(Dirty::GridMain);
		r.shell.tick(10);
		expect(r.fake.grid().dirty).to_be_true(); // pair 6 is still owed
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(10 + kBackpressureRetryMs));

		// The next tick, right at that deadline and with no new dirty fact, drains the remaining
		// pair -- `request_repaint()` alone (the old behaviour) would never have woken this up.
		r.shell.tick(10 + kBackpressureRetryMs);
		expect(r.fake.columns_sent.back().first).to_equal(uint8_t(6));
		expect(r.fake.grid().dirty).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(shell_flush_spec, shell_flush)
