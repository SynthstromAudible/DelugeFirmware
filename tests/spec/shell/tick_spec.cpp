#include "../gfx/support/golden.h"
#include "shell/shell.h"
#include "support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using deluge::notify::Dirty;

namespace {

/// The two kinds the test scenes register under: `FirstApp` is the base, the next one up the
/// overlay.
constexpr SceneKind kBase = SceneKind::FirstApp;
constexpr SceneKind kOverlay = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 1);

/// The colour the base scene paints every pad; picked so `>> 1` per channel is exact and so no
/// channel is symmetric with another.
constexpr RGB kBaseColour{100, 80, 60};
/// The colour the overlay scene paints its one pad.
constexpr RGB kOverlayColour{255, 0, 0};
/// Legacy's full greyout strength (`setGreyoutAmount(1)` -> `greyProportion = 1 * 6500000`,
/// `hid/led/pad_leds.cpp:753,786-788`), repeated here so the spec pins the *value* `render_grid`
/// is bound to rather than reusing whatever constant the implementation happens to define.
constexpr int32_t kGreyoutAmountFull = 6500000;

/// Board LED indices the LED-derivation case declares against; `LedId` is an opaque enum the board
/// profile gives meaning to, so a spec names its own.
constexpr deluge::ui::LedId kPlayLed = static_cast<deluge::ui::LedId>(3);

/// @brief A BUTTON event at matrix (x, y), as `input_lowering_spec.cpp` builds them.
DelugeInputEvent button_ev(uint8_t x, uint8_t y, bool on) {
	return DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)};
}

/// @brief The board profile the tick specs run against: `test_profile_input`'s matrix and encoders
///        on `kBoard2`'s two panels, so one rig can exercise both routing and per-role rendering.
/// The one string `tick_profile`'s `text` lookup resolves every id to.
constexpr std::string_view kPopupText = "SAVED";

BoardProfile tick_profile() {
	static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main, SurfaceRole::Aux};
	static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle48, &deluge::ui::kDelugeStyle64};
	BoardProfile p = uitest::test_profile_input();
	p.oled_role_by_index = kRoles;
	p.style_by_index = kStyles;
	p.text = +[](uint16_t) -> std::string_view { return kPopupText; };
	return p;
}

/// @brief Is pixel (x, y) of panel \p index lit?
///
/// Page-major, the same layout `page_major_to_pbm` decodes: `bytes[(y >> 3) * width + x]`, bit
/// `y & 7`.
bool lit(uitest::FakeSurfaceSet& fake, uint8_t index, int32_t x, int32_t y) {
	const OledSurface& s = fake.oled()[index];
	const std::span<const uint8_t> bytes = s.canvas.bytes();
	const size_t i = static_cast<size_t>(y >> 3) * s.geometry.width + static_cast<size_t>(x);
	return i < bytes.size() && (bytes[i] & (1u << (y & 7))) != 0;
}

/// @brief The base scene: paints every pad, draws a different thing on each OLED role, and counts
///        everything the shell does to it.
class BaseScene final : public Scene {
public:
	SceneKind kind() const override { return kBase; }

	void render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& frame) override {
		oled_renders++;
		if (role == SurfaceRole::Main) {
			frame.hline(kMainLineY, 0, 40);
			frame_ms = frame.now_ms();
			// Read (and therefore claim, per `FrameContext` rule 2) only when asked to, and only
			// in this one pass: reading the same input in `render_grid` as well would claim it
			// twice and trip `double_claim`.
			if (read_turn) {
				turns_seen += frame.turn();
			}
		}
		else {
			frame.rect({kAuxRectX, kAuxRectY, 20, 10});
		}
	}
	void render_grid(deluge::ui::grid::GridFrame& frame) override {
		grid_renders++;
		const deluge::gfx::grid::Rect b = frame.bounds();
		for (int32_t y = 0; y < b.h; y++) {
			for (int32_t x = 0; x < b.w; x++) {
				(void)frame.pad(x, y, kBaseColour);
			}
		}
	}
	void leds(deluge::ui::LedFrame& frame) override {
		(void)frame.set(kPlayLed, deluge::ui::LedState{deluge::ui::LedState::Kind::On, 0, 0});
	}
	Result handle(const UiEvent& event, const UiContext&) override {
		events++;
		last_event_kind = event.kind;
		if (emit_repaint_from_handle) {
			emit_repaint_from_handle = false;
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
			return r;
		}
		if (!emit_popup) {
			return Result::unhandled();
		}
		emit_popup = false;
		Result r;
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Popup, .popup_string = 42, .popup_type = 1});
		return r;
	}
	Result after_render() override {
		after_renders++;
		events_at_after_render = events;
		if (!emit_repaint_from_after_render) {
			return {};
		}
		emit_repaint_from_after_render = false;
		Result r;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
		return r;
	}
	void tick(uint32_t) override { ticks++; }
	void on_model_changed(const deluge::notify::DirtySet&) override { model_changed++; }
	std::optional<uint32_t> next_timer() const override { return timer; }
	bool timer_repaints() const override { return timer_repaints_; }

	/// The local row `render_oled` draws its Main rule on.
	static constexpr int32_t kMainLineY = 10;
	/// The local corner `render_oled` draws its Aux box at.
	static constexpr int32_t kAuxRectX = 4, kAuxRectY = 4;

	int oled_renders = 0, grid_renders = 0, events = 0, ticks = 0, model_changed = 0;
	int after_renders = 0, events_at_after_render = -1, turns_seen = 0;
	uint32_t frame_ms = 0;
	/// Whether `render_oled` reads (and claims) the select-encoder turn.
	bool read_turn = false;
	/// Whether the next `handle()` returns a `Popup` action.
	bool emit_popup = false;
	/// Whether the next `after_render()` returns `Repaint{Main}` (one-shot).
	bool emit_repaint_from_after_render = false;
	/// Whether the next `handle()` returns `Repaint{Main}` (one-shot), which is how a scene reacts
	/// to a widget-owned event the shell holds back and delivers after render.
	bool emit_repaint_from_handle = false;
	UiEvent::Kind last_event_kind = UiEvent::Kind::AllReleased;
	std::optional<uint32_t> timer;
	/// What `timer_repaints()` answers -- true, as the base class's own default is.
	bool timer_repaints_ = true;
};

/// @brief The scene stacked on top: a grid overlay by default, with every composition hook
///        settable so one class covers dim, greyout, occlusion and OLED transparency.
class OverlayScene final : public Scene {
public:
	SceneKind kind() const override { return kOverlay; }

	void render_oled(SurfaceRole, deluge::ui::oled::OledFrame& frame) override {
		oled_renders++;
		frame.rect({kBoxX, kBoxY, 30, 12});
	}
	void render_grid(deluge::ui::grid::GridFrame& frame) override {
		grid_renders++;
		(void)frame.pad(0, 0, kOverlayColour);
	}
	void leds(deluge::ui::LedFrame& frame) override {
		(void)frame.set(kPlayLed, deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, 200, 0});
	}
	Result handle(const UiEvent&, const UiContext&) override {
		events++;
		return Result::unhandled();
	}
	void tick(uint32_t) override { ticks++; }

	bool is_grid_overlay() const override { return overlay; }
	bool occludes_grid() const override { return occludes; }
	bool oled_transparent(SurfaceRole) const override { return transparent; }
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return mask; }

	/// The local corner `render_oled` draws its box at.
	static constexpr int32_t kBoxX = 60, kBoxY = 20;

	bool overlay = true, occludes = false, transparent = false;
	deluge::ui::grid::GreyoutMask mask{};
	int oled_renders = 0, grid_renders = 0, events = 0, ticks = 0;
};

/// @brief Build the registry table before it is handed to the `SceneRegistry` member, so the
///        `Shell` constructor -- which copies the table -- sees the scenes already in it.
std::array<Scene*, 18> table_for(Scene& base, Scene& overlay) {
	std::array<Scene*, 18> t{};
	t[static_cast<size_t>(kBase)] = &base;
	t[static_cast<size_t>(kOverlay)] = &overlay;
	return t;
}

/// @brief A `Shell` over `FakeSurfaceSet`, with the two test scenes registered.
///
/// Members are declared in construction order: the scenes before the table that points at them,
/// the table before the registry that views it, and the registry before the `Shell` that copies
/// it.
struct Rig {
	BoardProfile profile = tick_profile();
	BaseScene base;
	OverlayScene overlay;
	std::array<Scene*, 18> table = table_for(base, overlay);
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	Shell shell{fake, profile, reg, Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	Rig() = default;

	/// @brief What `Shell::flush` (Task 8) will do at the end of every tick: lower the pending
	///        flags, so the next tick's are visible.
	void consume_pending() {
		for (OledSurface& s : fake.oled()) {
			s.blit_pending = false;
		}
		fake.grid().dirty = false;
	}
};

} // namespace

// clang-format off
describe shell_tick("shell::Shell::tick", $ {
	it("a clean tick produces no blit and no column sends after the first frame", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});

		r.shell.tick(0);
		// `Shell::flush` runs at the end of every tick now, so the first tick's render is already
		// pushed out by the time it returns: one blit per OLED surface, and every one of
		// `kBoard2`'s 9 column pairs (18 total columns), the base scene having painted every pad a
		// colour that differs from the all-black `last_sent` every pair starts at.
		expect(r.fake.blits.size()).to_equal(size_t(2));
		expect(r.fake.columns_sent.size()).to_equal(size_t(9));
		expect(r.fake.oled()[0].blit_pending).to_be_false();
		expect(r.fake.oled()[1].blit_pending).to_be_false();
		expect(r.fake.grid().dirty).to_be_false();
		expect(r.base.oled_renders).to_equal(2);
		expect(r.base.grid_renders).to_equal(1);

		// Nothing at all pending -- no input, no dirty facts, no stack change, no armed deadline --
		// so the second tick neither renders nor sends anything new.
		r.shell.tick(10);
		expect(r.fake.blits.size()).to_equal(size_t(2));
		expect(r.fake.columns_sent.size()).to_equal(size_t(9));
		expect(r.base.oled_renders).to_equal(2);
		expect(r.base.grid_renders).to_equal(1);
	});

	it("model dirtiness re-renders the surface it names, and reaches on_model_changed", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.consume_pending();
		expect(r.base.model_changed).to_equal(0);

		// A grid-only fact repaints the grid and leaves the OLED alone: `apply_dirty` requests
		// exactly the gates the fact names, and `needs_frame`'s `model_dirty` is passed `false` so
		// nothing widens that back out to every surface. (The base scene paints the same solid
		// colour every time, so the re-render sends no new column pairs -- `grid_renders` is the
		// signal that the grid rendered again, and `columns_sent` staying flat confirms the diff
		// correctly found nothing changed rather than merely not being exercised.)
		const size_t columns_before_grid_fact = r.fake.columns_sent.size();
		deluge::notify::dirty().raise(Dirty::ClipContent);
		r.shell.tick(20);
		expect(r.base.model_changed).to_equal(1);
		expect(r.base.grid_renders).to_equal(2);
		expect(r.fake.columns_sent.size()).to_equal(columns_before_grid_fact);
		expect(r.base.oled_renders).to_equal(2); // still just the first tick's two
		expect(r.fake.oled()[0].blit_pending).to_be_false();
		expect(r.fake.oled()[1].blit_pending).to_be_false();

		// An OLED-only fact is the mirror image: both panels re-render and (being unbusy) reblit.
		r.consume_pending();
		const size_t blits_before_oled_fact = r.fake.blits.size();
		deluge::notify::dirty().raise(Dirty::Oled);
		r.shell.tick(30);
		expect(r.base.oled_renders).to_equal(4);
		expect(r.fake.blits.size()).to_be_greater_than(blits_before_oled_fact);
		expect(r.base.grid_renders).to_equal(2);
		expect(r.fake.grid().dirty).to_be_false();

		// A fact that names no surface still reaches every gate.
		r.consume_pending();
		const size_t blits_before_untargeted_fact = r.fake.blits.size();
		const size_t columns_before_untargeted_fact = r.fake.columns_sent.size();
		deluge::notify::dirty().raise(Dirty::PlaybackState);
		r.shell.tick(40);
		expect(r.base.oled_renders).to_equal(6);
		expect(r.base.grid_renders).to_equal(3);
		expect(r.fake.columns_sent.size()).to_equal(columns_before_untargeted_fact);
		expect(r.fake.blits.size()).to_be_greater_than(blits_before_untargeted_fact);
	});

	it("input requests a frame and routes to the top scene", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.consume_pending();

		// Play, at matrix (8, 3) -- `definitions_cxx.hpp:68`, mirrored in `test_profile_input`.
		const size_t blits_before = r.fake.blits.size();
		r.shell.on_board_event(button_ev(8, 3, true), 25);
		r.shell.tick(30);
		expect(r.base.events).to_equal(1);
		expect(r.fake.blits.size()).to_be_greater_than(blits_before);
		expect(r.base.grid_renders).to_equal(2);
		expect(r.base.oled_renders).to_equal(4);
	});

	it("overlay dims the base by right-shift and composites over it", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();

		r.shell.tick(0);
		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		expect(g.at(0, 0) == kOverlayColour).to_be_true();
		expect(g.at(1, 0) == RGB{50, 40, 30}).to_be_true();
		expect(g.at(17, 7) == RGB{50, 40, 30}).to_be_true();
	});

	it("greyout mask from the stack is applied", _ {
		deluge::notify::dirty().drain();
		Rig r;
		// `GreyoutMask::cols` is counted right-to-left -- bit `n` selects column `width - 1 - n`
		// (`ui/grid/effects.h`, from `prepareColour`'s `kDisplayWidth + kSideBarWidth - 1 - x`) --
		// so column 0 of an 18-wide grid is bit 17, not bit 0.
		r.overlay.mask = deluge::ui::grid::GreyoutMask{.cols = 1u << 17, .rows = 0};
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.shell.tick(0);

		expect(r.shell.greyout().cols).to_equal(uint32_t(1u << 17));
		RGB dimmed{50, 40, 30};
		const RGB greyed = dimmed.greyOut(kGreyoutAmountFull);
		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		expect(g.at(0, 1) == greyed).to_be_true();
		expect(g.at(1, 1) == RGB{50, 40, 30}).to_be_true();
		// Greyout runs last, so even the overlay's own pad is tinted.
		RGB overlay_colour = kOverlayColour;
		expect(g.at(0, 0) == overlay_colour.greyOut(kGreyoutAmountFull)).to_be_true();
	});

	it("occludes_grid stops ticking scenes beneath", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.overlay.occludes = true;
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();

		r.shell.tick(0);
		r.shell.tick(10);
		expect(r.overlay.ticks).to_equal(2);
		expect(r.base.ticks).to_equal(0);
	});

	it("oled_transparent lets the scene below show through", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.overlay.transparent = true;
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.shell.tick(0);

		// Both layers drew: the base's Main rule (local y 10, and the 48px panel's frame origin is
		// its 5px `visible_top`) and the overlay's box on top of it.
		expect(r.base.oled_renders).to_equal(2);
		expect(r.overlay.oled_renders).to_equal(2);
		const int32_t top = r.fake.oled()[0].geometry.visible_top;
		expect(lit(r.fake, 0, 0, top + BaseScene::kMainLineY)).to_be_true();
		expect(lit(r.fake, 0, 40, top + BaseScene::kMainLineY)).to_be_true();
		expect(lit(r.fake, 0, OverlayScene::kBoxX, top + OverlayScene::kBoxY)).to_be_true();
		expect_golden(self, "../shell/overlay_48.pbm", r.fake.pbm(0));

		// Made opaque, the same overlay stops the base being drawn at all.
		deluge::notify::dirty().drain();
		Rig opaque;
		opaque.overlay.transparent = false;
		opaque.shell.init({kBase, 0});
		expect(opaque.shell.stack().push({kOverlay, 0})).to_be_true();
		opaque.shell.tick(0);
		expect(opaque.base.oled_renders).to_equal(0);
		expect(lit(opaque.fake, 0, 0, top + BaseScene::kMainLineY)).to_be_false();
	});

	it("next_wake is the earliest gate deadline or scene timer", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		expect(r.shell.next_wake().has_value()).to_be_false();

		// A gate deadline is absolute; a scene timer is relative to the tick it is read on.
		r.base.timer = 50;
		r.fake.oled()[0].gate.request_repaint_after(15, 10);
		r.shell.tick(10);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(25));

		r.base.timer = 2;
		r.shell.tick(20);
		expect(*r.shell.next_wake()).to_equal(uint32_t(22));
	});

	it("a scene timer repaints every surface the scene is on, with no gate arm of its own", _ {
		// `BaseScene` arms nothing from `render_*`. Before E3 task 10 that made `next_timer()` a
		// bare wake: the shell came back, `RepaintGate::needs_frame` said no on every surface, and
		// the scene's state advanced against a framebuffer that never changed. Four scenes shipped
		// or nearly shipped that way (`Scene::next_timer`'s history), so step 8b of `Shell::tick`
		// now folds the two together.
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0); // the first frame every gate gives away
		const int oled_after_first = r.base.oled_renders;
		const int grid_after_first = r.base.grid_renders;

		// No timer: quiet ticks draw nothing at all. This is the control -- without it the
		// assertion below could pass on a shell that simply renders every tick.
		r.shell.tick(10);
		r.shell.tick(20);
		expect(r.base.oled_renders).to_equal(oled_after_first);
		expect(r.base.grid_renders).to_equal(grid_after_first);

		// A 20 ms timer, and the frames follow it: nothing at 30 (the deadline armed at 20 is 40),
		// both surfaces at 40. Two OLED renders per frame, because this rig has two panels.
		r.base.timer = 20;
		r.shell.tick(20);
		r.shell.tick(30);
		expect(r.base.oled_renders).to_equal(oled_after_first);
		expect(r.base.grid_renders).to_equal(grid_after_first);
		r.shell.tick(40);
		expect(r.base.oled_renders).to_equal(oled_after_first + 2);
		expect(r.base.grid_renders).to_equal(grid_after_first + 1);

		// And it keeps going: the arm is re-filed at the bottom of every tick, so the frame at 40
		// arms 60, not a one-shot that stops there.
		r.shell.tick(50);
		expect(r.base.grid_renders).to_equal(grid_after_first + 1);
		r.shell.tick(60);
		expect(r.base.oled_renders).to_equal(oled_after_first + 4);
		expect(r.base.grid_renders).to_equal(grid_after_first + 2);
	});

	it("timer_repaints() false is a wake with no repaint, and still reaches next_wake", _ {
		// The opt-out: a timer whose consequence is not on screen. It must still wake the shell --
		// `compute_next_wake`'s scene loop exists for exactly these scenes, every other timer being
		// represented by the gate deadline step 8b armed -- and it must not dirty anything.
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		const int oled_after_first = r.base.oled_renders;
		const int grid_after_first = r.base.grid_renders;

		r.base.timer = 20;
		r.base.timer_repaints_ = false;
		r.shell.tick(20);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(40)); // the wake is kept...
		r.shell.tick(40);
		expect(r.base.ticks).to_be_greater_than(0);
		expect(r.base.oled_renders).to_equal(oled_after_first); // ...and nothing was drawn for it
		expect(r.base.grid_renders).to_equal(grid_after_first);
	});

	it("renders each OLED surface with its own role and geometry", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		// Main is the 48px panel with a 5px faceplate; Aux the 64px one with none.
		expect(r.fake.oled()[0].geometry.visible_top).to_equal(uint8_t(5));
		expect(r.fake.oled()[1].geometry.visible_top).to_equal(uint8_t(0));
		expect(lit(r.fake, 0, 0, 5 + BaseScene::kMainLineY)).to_be_true();
		expect(lit(r.fake, 0, BaseScene::kAuxRectX, BaseScene::kAuxRectY)).to_be_false();
		expect(lit(r.fake, 1, BaseScene::kAuxRectX, BaseScene::kAuxRectY)).to_be_true();
		expect(lit(r.fake, 1, 0, 5 + BaseScene::kMainLineY)).to_be_false();
		expect(r.fake.pbm(0) != r.fake.pbm(1)).to_be_true();
	});

	it("derives the LED surface from the composed frame, top scene winning", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		const size_t play = static_cast<size_t>(static_cast<uint8_t>(kPlayLed));
		// Base alone: On.
		expect(r.fake.leds().desired_declared[play]).to_be_true();
		expect(r.fake.leds().desired_on[play]).to_be_true();

		// Overlay on top declares the same LED as a 200 ms blink, so the phase now decides.
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.shell.tick(200);
		expect(r.fake.leds().desired_on[play]).to_be_false();
		r.shell.tick(400);
		expect(r.fake.leds().desired_on[play]).to_be_true();
	});

	it("a notice scene overlays the grid rather than replacing it", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		// The shell owns SceneKind::Popup, so this pushes its own PopupScene.
		expect(r.shell.stack().push({SceneKind::Popup, 0})).to_be_true();
		r.shell.tick(0);

		// The base still painted: a notice is a grid overlay that paints nothing, not the base.
		expect(r.base.grid_renders).to_equal(1);
		// And it is left exactly as the base painted it: a notice composites nothing on the grid,
		// so it opts out of the overlay dim (`Scene::dims_grid`). Legacy's popups have no grid
		// effect at all, and halving every pad to announce a message would be a regression.
		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		expect(g.at(3, 3) == kBaseColour).to_be_true();
		expect(g.at(17, 7) == kBaseColour).to_be_true();
	});

	it("an overlay that composites pads still dims the base beneath it", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		// A real grid overlay -- one that paints -- takes the default `dims_grid()`.
		expect(r.overlay.dims_grid()).to_be_true();
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		// ...and a notice above it does not undo that: the dim asks "does *any* overlay dim".
		expect(r.shell.stack().push({SceneKind::Popup, 0})).to_be_true();
		r.shell.tick(0);

		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		expect(g.at(3, 3) == RGB{50, 40, 30}).to_be_true();
		expect(g.at(0, 0) == kOverlayColour).to_be_true();
	});

	it("a passive overlay leaves the scene beneath focused and routed", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.read_turn = true;
		r.shell.init({kBase, 0});
		// The console strip: `oled_transparent` on every role, so the base is still drawn beneath
		// it -- which is what makes the focus question visible at all. (A popup is opaque on Main,
		// so the base is not drawn there; the routing half below holds for either.)
		expect(r.shell.stack().push({SceneKind::Console, 0})).to_be_true();
		r.shell.tick(0);
		r.consume_pending();

		// Select turn (board encoder index 3, two A-pin edges = one detent) and a Play press, both
		// fed while the console is the stack's top.
		r.shell.on_encoder_edges(3, 2, 25);
		r.shell.on_board_event(button_ev(8, 3, true), 25);
		r.shell.tick(30);

		// The base's frame was built with InputMask::all() despite the console above it...
		expect(r.base.turns_seen).to_equal(1);
		// ...and the router reached the base's handle(), not the popup's.
		expect(r.base.events).to_equal(1);
		expect(r.base.last_event_kind == UiEvent::Kind::Button).to_be_true();
	});

	it("the frame clock is the tick's, not the feed's", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.shell.on_board_event(button_ev(8, 3, true), 25);
		r.shell.tick(30);
		expect(r.base.frame_ms).to_equal(uint32_t(30));
		expect(r.shell.lowering().snapshot().now_ms).to_equal(uint32_t(30));
		expect(r.shell.lowering().ctx().tick_ms).to_equal(uint32_t(30));
	});

	it("a held-back event claimed during render is not routed; an unclaimed one is", _ {
		deluge::notify::dirty().drain();
		Rig claimed;
		claimed.base.read_turn = true;
		claimed.shell.init({kBase, 0});
		claimed.shell.tick(0);
		claimed.shell.on_encoder_edges(3, 2, 25);
		claimed.shell.tick(30);
		expect(claimed.base.turns_seen).to_equal(1);
		expect(claimed.base.events).to_equal(0); // the frame owned it

		deluge::notify::dirty().drain();
		Rig unclaimed;
		unclaimed.base.read_turn = false;
		unclaimed.shell.init({kBase, 0});
		unclaimed.shell.tick(0);
		unclaimed.shell.on_encoder_edges(3, 2, 25);
		unclaimed.shell.tick(30);
		expect(unclaimed.base.events).to_equal(1);
		expect(unclaimed.base.last_event_kind == UiEvent::Kind::Encoder).to_be_true();
		// Delivered after render *and* after this tick's after_render(): the claim is only knowable
		// once the frame that could have made it has been built and acted on.
		expect(unclaimed.base.after_renders).to_equal(2);
		expect(unclaimed.base.events_at_after_render).to_equal(0);
	});

	it("a Popup action becomes the popup scene, resolved through the profile's text", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.base.emit_popup = true;
		r.shell.on_board_event(button_ev(8, 3, true), 25);
		r.shell.tick(30);

		expect(r.shell.stack().top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.shell.notices().last_popup_text())).to_equal(std::string(kPopupText));
		expect(r.shell.state_lines())
		    .to_equal(std::string("scene Popup/General\nscene app16\nnotice Text SAVED\n"));
	});

	it("a Repaint from after_render is honoured on the next tick", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.consume_pending();

		// `after_render()` only runs for a scene that rendered, so give this tick a reason to
		// render: an OLED fact draws both panels, and the base's `after_render` then asks for Main
		// again. The request is raised at step 7 -- after every render decision this tick has
		// already been made -- so it is for the *next* tick, and the shell must both keep it and
		// arrange to be woken for it.
		r.base.emit_repaint_from_after_render = true;
		deluge::notify::dirty().raise(Dirty::Oled);
		r.shell.tick(10);
		const int renders_after_the_asking_tick = r.base.oled_renders;
		expect(r.base.after_renders).to_be_greater_than(0);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(10)); // "now": there is work waiting
		r.consume_pending();

		// No input, no dirty fact, no stack change: the *only* thing that can draw this tick is the
		// repaint `after_render` asked for, and it draws only Main.
		const size_t blits_before = r.fake.blits.size();
		r.shell.tick(20);
		expect(r.base.oled_renders).to_equal(renders_after_the_asking_tick + 1);
		expect(r.fake.blits.size()).to_equal(blits_before + 1);
		expect(r.base.grid_renders).to_equal(1); // Main only; the grid was never asked for

		// And it is a one-shot: nothing re-requested it, so the tick after is clean again.
		r.consume_pending();
		r.shell.tick(30);
		expect(r.base.oled_renders).to_equal(renders_after_the_asking_tick + 1);
	});

	it("a Repaint from a post-render-delivered event is honoured on the next tick", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.read_turn = false; // leave the detent unclaimed, so it reaches handle() after render
		r.base.emit_repaint_from_handle = true;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.consume_pending();

		// A select turn is one of the six widget-owned shapes: the shell holds it back and delivers
		// it at step 8, after the frame is built. The scene advances its selection there and asks
		// for Main -- the shape of a menu that does not move when the knob does.
		r.shell.on_encoder_edges(3, 2, 25);
		r.shell.tick(30);
		expect(r.base.events).to_equal(1);
		const int renders_after_the_asking_tick = r.base.oled_renders;
		expect(*r.shell.next_wake()).to_equal(uint32_t(30));
		r.consume_pending();

		const size_t blits_before = r.fake.blits.size();
		r.shell.tick(40);
		expect(r.base.oled_renders).to_equal(renders_after_the_asking_tick + 1);
		expect(r.fake.blits.size()).to_equal(blits_before + 1);
	});

	it("events deferred by the SD lock are routed once the lock is released", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.shell.set_sd_busy(true);
		// Back, at matrix (7, 1). Deliberately a *widget-owned* shape: an ordinary button survives
		// a lost window anyway, because `push_out` puts it on the routed FIFO, which `begin_tick`
		// does not clear. Back goes to `held_back_`, which it does -- along with `had_input_this_tick`
		// and the tick's detent sums -- so this is the shape that shows whether the replay landed
		// in an open window.
		r.shell.on_board_event(button_ev(7, 1, true), 20);
		r.shell.tick(30);
		expect(r.base.events).to_equal(0); // the lock held it
		r.consume_pending();

		// Released between ticks, with the lowering's tick window closed by the tick above: the
		// replay has to open one, or the next `begin_tick` clears every replayed edge before
		// anything can route it.
		const size_t blits_before = r.fake.blits.size();
		r.shell.set_sd_busy(false);
		r.shell.tick(40);
		expect(r.base.events).to_equal(1);
		expect(r.base.last_event_kind == UiEvent::Kind::Button).to_be_true();
		// The replay also counts as input, so the tick that delivers it renders a frame and, being
		// unbusy, flush blits it straight away.
		expect(r.fake.blits.size()).to_be_greater_than(blits_before);
	});

	it("state_lines reports the stack top first, then the lock", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 7});
		expect(r.shell.stack().push({kOverlay, 3})).to_be_true();
		expect(r.shell.state_lines()).to_equal(std::string("scene app17/3\nscene app16/7\n"));

		r.shell.lowering().set_input_lock(true);
		expect(r.shell.state_lines()).to_equal(std::string("scene app17/3\nscene app16/7\nlock input\n"));
	});
});
// clang-format on

DELUGE_SPEC(shell_tick_spec, shell_tick)
