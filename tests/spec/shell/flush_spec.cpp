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
		(void)frame.set(kPlayLed,
		                play_override.value_or(deluge::ui::LedState{
		                    play_on ? deluge::ui::LedState::Kind::On : deluge::ui::LedState::Kind::Off, 0, 0}));
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
	/// When set, `leds()` declares `kPlayLed` with this state instead of the plain on/off pair --
	/// the blink-wake example below is the only user.
	std::optional<deluge::ui::LedState> play_override;
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

/// @brief A `Service` with a timer and a per-surface repaint claim -- what E4 task 5 added
///        `Service::timer_repaints()` for.
///
/// `services::MidiLearn`'s flash is the real case: `View::midiLearnFlash` toggles
/// `midiLearnFlashOn` and then asks the ROOT UI to repaint
/// (`src/deluge/gui/views/view.cpp:814-819`, `getRootUI()->midiLearnFlash()`), and every reader of
/// that bit in legacy is a pad renderer. Before the fold a service timer fed `compute_next_wake`
/// and nothing else, so the phase advanced on the wake and no surface was ever dirtied.
struct TimerService final : Service {
	std::optional<uint32_t> next_timer() const override { return timer; }
	Scene::RepaintSurfaces timer_repaints() const override { return claim; }

	std::optional<uint32_t> timer{};
	Scene::RepaintSurfaces claim{.oled = false, .grid = false};
};

/// @brief A `Service` whose `timer_repaints()` is written the way an author naturally writes it:
///        one designated initializer naming the one surface the timer actually moves.
///
/// This double exists to be the E4 final review's I1 in executable form. `services::MidiLearn`
/// spells both fields (`{.oled = false, .grid = true}`), so nothing in the tree exercised what an
/// *unnamed* member means -- and while `Scene::RepaintSurfaces`' members defaulted to `true`, it
/// meant "arm it". A service claiming the grid alone would therefore have armed every OLED panel
/// at its timer's cadence, unconditionally and forever, which is the regression E3 shipped twice
/// on the scene side (50 Hz, then 100 Hz, both two-panel).
///
/// The claim is a fixed `return {.grid = true};` rather than a settable member on purpose: the
/// thing under test is the *spelling*, so it has to appear literally.
struct GridOnlyTimerService final : Service {
	std::optional<uint32_t> next_timer() const override { return timer; }
	Scene::RepaintSurfaces timer_repaints() const override { return {.grid = true}; }

	std::optional<uint32_t> timer{};
};

/// @brief `Rig`, with one service occupying the `midi_learn` slot.
struct ServiceRig {
	BoardProfile profile = flush_profile();
	FlushScene base;
	TimerService service;
	std::array<Scene*, 18> table = table_for(base);
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	Shell shell{fake, profile, reg, Services{.midi_learn = &service}, ShellConfig{}};

	ServiceRig(const ServiceRig&) = delete;
	ServiceRig& operator=(const ServiceRig&) = delete;
	ServiceRig() = default;
};

/// @brief `Rig`, with the natural-override service in the `midi_learn` slot.
struct NaturalServiceRig {
	BoardProfile profile = flush_profile();
	FlushScene base;
	GridOnlyTimerService service;
	std::array<Scene*, 18> table = table_for(base);
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	Shell shell{fake, profile, reg, Services{.midi_learn = &service}, ShellConfig{}};

	NaturalServiceRig(const NaturalServiceRig&) = delete;
	NaturalServiceRig& operator=(const NaturalServiceRig&) = delete;
	NaturalServiceRig() = default;
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

	it("next_wake sees a declared blink, so the phase moves with no input at all", _ {
		// **A declared blink is a wake source** (E4 task 4 fix round 1). `led_is_on` derives a
		// blink's phase from `now_ms` alone (`shell/leds.cpp`), and every other source
		// `compute_next_wake` consults is something a scene or service asked for -- so before this
		// fix a declared `Kind::Blink` froze at whatever phase the last tick left it in on an
		// otherwise idle system. Live rather than theoretical: `MenuScene::leds` declares BACK and
		// LEARN as `Blink(kFlashTime)` unconditionally (`scenes/menu/menu_scene.cpp:1335`, `:1342`)
		// while `MenuScene::next_timer` (`scenes/menu/menu_scene.cpp:2162-2176`) answers `nullopt` for
		// any focused node with neither source pads nor a shortcut position.
		//
		// `FlushScene` has no `next_timer()` of its own and this rig arms no gate past the first
		// tick, so the blink is the ONLY thing that could produce a wake here -- which is what
		// makes the example fail without the fix rather than pass by accident.
		deluge::notify::dirty().drain();
		Rig r;
		r.base.play_override = deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, 110, 0};
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(110));

		// And the LED really does change on that schedule, with no input: phase 0 is lit, phase 1
		// is not, and the shell sends the difference.
		r.fake.led_writes.clear();
		r.shell.tick(110);
		expect(r.fake.led_writes.size()).to_equal(size_t(1));
		expect(r.fake.led_writes[0].first).to_equal(kPlayBoardLed);
		expect(r.fake.led_writes[0].second).to_be_false();
		expect(*r.shell.next_wake()).to_equal(uint32_t(220));

		r.fake.led_writes.clear();
		r.shell.tick(220);
		expect(r.fake.led_writes.size()).to_equal(size_t(1));
		expect(r.fake.led_writes[0].second).to_be_true();

		// A bounded blink wakes at its own deadline too, and stops asking once it has latched
		// solid -- `led_is_on`'s `until_ms` arm.
		r.base.play_override = deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, 110, 250};
		r.shell.tick(230);
		expect(*r.shell.next_wake()).to_equal(uint32_t(250)); // the deadline beats the 330 boundary
		r.shell.tick(250);
		expect(r.shell.next_wake().has_value()).to_be_false();
	});

	it("a service timer arms only the surfaces it claims -- grid yes, OLED never", _ {
		// E4 task 5. `Service::timer_repaints()` defaults to `{false, false}` so nothing that
		// existed before this fold changed behaviour, and `services::MidiLearn` claims
		// `{.oled = false, .grid = true}` because the MIDI-learn flash is a pad concern
		// (`gui/views/view.cpp:814-819` asks the ROOT UI to repaint, and every reader of
		// `midiLearnFlashOn` is a pad renderer).
		//
		// **The OLED half is the one that matters.** The grid's `flush` diffs against `last_sent`,
		// so an unneeded grid arm costs a repaint and no bytes; an OLED surface is blitted
		// unconditionally once it has rendered, so an unneeded OLED arm is a permanent blit at the
		// timer's cadence for as long as the service asks (see `Scene::timer_repaints`).
		deluge::notify::dirty().drain();
		ServiceRig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		// --- Claiming nothing: the timer is a wake and only a wake ---------------------------
		r.service.timer = 60;
		r.shell.tick(1);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(61));
		r.fake.blits.clear();
		r.fake.columns_sent.clear();
		r.shell.tick(61);
		expect(r.fake.blits.empty()).to_be_true();        // no OLED surface armed
		expect(r.fake.columns_sent.empty()).to_be_true(); // and no grid pair either

		// --- Claiming the grid: the gate really fires at the deadline -------------------------
		r.service.claim = Scene::RepaintSurfaces{.oled = false, .grid = true};
		// A colour the grid has never sent, so the repaint the arm buys is observable through the
		// diff rather than through a count -- the grid's flush sends nothing for an unchanged
		// frame, which is exactly why over-arming it is cheap.
		r.base.colour = RGB{99, 88, 77};
		r.shell.tick(100);
		r.fake.blits.clear();
		r.fake.columns_sent.clear();
		r.base.colour = RGB{1, 2, 3};
		r.shell.tick(160); // 100 + the 60 ms the service asked for
		expect(r.fake.columns_sent.empty()).to_be_false(); // the grid repainted and the diff sent
		expect(r.fake.blits.empty()).to_be_true();         // and the OLED panels stayed asleep

		// --- Claiming the OLED: the other branch, and the one that had only a docstring -------
		//
		// **This leg exists because the first cut of this example was a mutation survivor**
		// (task-5 fix round 1). It set only `{false, false}` and `{false, true}`, so deleting the
		// whole `if (surfaces.oled)` block from `Shell::tick` step 8c left all 98 host tests green
		// -- on the exact branch whose scene-side twin shipped a 50 Hz and then a 100 Hz two-panel
		// blit in E3. The scene fold at least has `oled_paintable()` standing behind it; the
		// service fold has no narrowing at all, so its only machine check is this.
		r.service.claim = Scene::RepaintSurfaces{.oled = true, .grid = false};
		// Distinct content on BOTH surfaces, so "the OLED repainted" and "the grid did not" are
		// each observable rather than inferred: the OLED blits whatever it rendered, and the grid
		// sends only what its diff finds changed.
		r.base.colour = RGB{200, 150, 100};
		r.shell.tick(300);
		r.fake.blits.clear();
		r.fake.columns_sent.clear();
		r.base.oled_renders = 0;
		r.base.colour = RGB{7, 8, 9};
		r.shell.tick(360); // 300 + the 60 ms the service asked for
		expect(r.base.oled_renders > 0).to_be_true();      // the OLED gate fired and the scene drew
		expect(r.fake.blits.empty()).to_be_false();        // …and the panels were blitted
		expect(r.fake.columns_sent.empty()).to_be_true();  // the grid gate was NOT armed
		// Both panels, not just the first: the loop arms every OLED surface a service claims,
		// because a service has no stack position to narrow by.
		expect(r.fake.blits.size()).to_equal(r.fake.oled().size());

		// --- And a service with no timer arms nothing, however it claims ---------------------
		//
		// A FRESH rig, deliberately: the tick above still had a timer, so it armed the gate for
		// 220 before this half could clear it, and asserting on the same rig would be asserting
		// against that standing deadline rather than against the claim. (Found by writing it the
		// other way first and watching it fail.)
		deluge::notify::dirty().drain();
		ServiceRig quiet;
		quiet.service.claim = Scene::RepaintSurfaces{.oled = false, .grid = true};
		quiet.shell.init({kBase, 0});
		quiet.shell.tick(0);
		quiet.fake.blits.clear();
		quiet.fake.columns_sent.clear();
		quiet.base.colour = RGB{4, 5, 6};
		quiet.shell.tick(500);
		expect(quiet.fake.columns_sent.empty()).to_be_true();
		expect(quiet.fake.blits.empty()).to_be_true();
		expect(quiet.shell.next_wake().has_value()).to_be_false();
	});

	it("a service claiming {.grid = true} arms no OLED gate -- the unnamed member is not a claim", _ {
		// E4 final review, I1. The leg above proves the shell HONOURS a claim; this one proves the
		// claim is what the author wrote. `RepaintSurfaces`' members default to `false`, so
		// `return {.grid = true};` -- the shape every author reaches for first -- leaves `oled`
		// false. Before the fix the members defaulted to `true`, this example blitted both panels
		// every 60 ms forever, and nothing in the tree noticed because every existing override
		// happens to spell both fields.
		//
		// Read first, as a type property, so the failure names the cause rather than a blit count:
		expect(Scene::RepaintSurfaces{}.oled).to_be_false();
		expect(Scene::RepaintSurfaces{}.grid).to_be_false();
		expect(Scene::RepaintSurfaces{}.any()).to_be_false();
		expect(Scene::RepaintSurfaces{.grid = true}.oled).to_be_false();
		expect(Scene::RepaintSurfaces{.oled = true}.grid).to_be_false();

		deluge::notify::dirty().drain();
		NaturalServiceRig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		expect(r.service.timer_repaints().grid).to_be_true();
		expect(r.service.timer_repaints().oled).to_be_false();

		// …and the same answer through the shell, at the deadline. Distinct content on both
		// surfaces so each half is observed rather than inferred, exactly as the leg above does.
		r.service.timer = 60;
		r.base.colour = RGB{200, 150, 100};
		r.shell.tick(100);
		r.fake.blits.clear();
		r.fake.columns_sent.clear();
		r.base.oled_renders = 0;
		r.base.colour = RGB{7, 8, 9};
		r.shell.tick(160); // 100 + the 60 ms the service asked for
		expect(r.fake.columns_sent.empty()).to_be_false(); // the grid it named repainted
		expect(r.base.oled_renders).to_equal(0);           // the OLED it did not name never drew
		expect(r.fake.blits.empty()).to_be_true();         // …so no panel was blitted

		// The other half of the contract, and the reason the `false` members live on the TYPE and
		// the `{true, true}` default lives in `Scene::timer_repaints()`'s BODY: a scene that
		// overrides nothing still arms both. `FlushScene` overrides neither hook.
		expect(r.base.timer_repaints().oled).to_be_true();
		expect(r.base.timer_repaints().grid).to_be_true();
		// A service that overrides nothing still arms nothing.
		expect(NullService{}.timer_repaints().any()).to_be_false();
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
