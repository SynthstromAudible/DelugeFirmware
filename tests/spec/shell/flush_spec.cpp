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

	RGB colour{10, 20, 30};
	std::vector<std::pair<int32_t, RGB>> column_overrides;
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

		// The retry, with no dirty fact raised again: the backpressure branch re-armed the gate.
		r.shell.tick(20);
		expect(r.fake.columns_sent.size()).to_equal(size_t(11));
		expect(r.fake.columns_sent.back().first).to_equal(uint8_t(6));
		expect(r.fake.grid().dirty).to_be_false();
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

	it("mirror sees the main surface after a blit, throttled", _ {
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

		// A reblit inside the 100 ms throttle window is not mirrored again.
		deluge::notify::dirty().raise(Dirty::Oled);
		r.shell.tick(50);
		expect(r.fake.blits.size()).to_equal(size_t(4)); // 2 surfaces x 2 ticks
		expect(calls.size()).to_equal(size_t(1));

		// Past the interval, the next blit is mirrored.
		deluge::notify::dirty().raise(Dirty::Oled);
		r.shell.tick(150);
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
});
// clang-format on

DELUGE_SPEC(shell_flush_spec, shell_flush)
