// The shell's headers reach libdeluge/types.h, whose DelugeFileLocation::context the cppspec `context` macro would
// rewrite, so they come before anything that includes cppspec.
#include "notify/notice_queue.h"
#include "shell/shell.h"
#include "support/fake_surfaces.h"

#include "../gfx/support/golden.h"
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
/// A second overlay kind, for the cases that stack two overlays.
constexpr SceneKind kUpper = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 2);

/// The colour the base scene paints every pad; picked so `>> 1` per channel is exact and so no
/// channel is symmetric with another.
constexpr RGB kBaseColour{100, 80, 60};
/// The colour the overlay scene paints its one pad.
constexpr RGB kOverlayColour{255, 0, 0};
/// Legacy's full greyout strength (`setGreyoutAmount(1)` -> `greyProportion = 1 * 6500000`,
/// `hid/led/pad_leds.cpp`), repeated here so the spec pins the *value* `render_grid`
/// is bound to rather than reusing whatever constant the implementation happens to define.
constexpr int32_t kGreyoutAmountFull = 6500000;

/// Board LED indices the LED-derivation case declares against; `LedId` is an opaque enum the board
/// profile gives meaning to, so a spec names its own.
constexpr deluge::ui::LedId kPlayLed = static_cast<deluge::ui::LedId>(3);
/// Two more, for the shared-blink cases.
constexpr deluge::ui::LedId kRecordLed = static_cast<deluge::ui::LedId>(4);
constexpr deluge::ui::LedId kSongLed = static_cast<deluge::ui::LedId>(5);

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
			events_at_render = events;
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
				if (frame.pad(x, y, colour).pressed) {
					pads_pressed++;
				}
			}
		}
	}
	void leds(deluge::ui::LedFrame& frame) override {
		(void)frame.set(kPlayLed, deluge::ui::LedState{deluge::ui::LedState::Kind::On, 0, 0});
		if (shared_blink) {
			(void)frame.set(kRecordLed, deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal));
		}
	}
	Result handle(const UiEvent& event, const UiContext&) override {
		events++;
		last_event_kind = event.kind;
		order += event.kind == UiEvent::Kind::Pad ? 'P' : event.kind == UiEvent::Kind::Encoder ? 'E' : 'B';
		if (emit_repaint_from_handle) {
			emit_repaint_from_handle = false;
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
			return r;
		}
		if (push_overlay_from_handle) {
			push_overlay_from_handle = false;
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::Push, .id = SceneId{kOverlay, 0}});
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
		if (raise_in_after_render) {
			raise_in_after_render = false;
			deluge::notify::dirty().raise(Dirty::Oled);
		}
		if (!emit_repaint_from_after_render) {
			return {};
		}
		emit_repaint_from_after_render = false;
		Result r;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
		return r;
	}
	void tick(uint32_t) override {
		ticks++;
		if (raise_in_tick) {
			raise_in_tick = false;
			deluge::notify::dirty().raise(Dirty::Oled);
		}
	}
	bool owns(const UiEvent& event) const override {
		return owns_turn && event.kind == UiEvent::Kind::Encoder && event.encoder == deluge::ui::EncoderRole::Select;
	}
	void on_model_changed(const deluge::notify::DirtySet&) override { model_changed++; }
	std::optional<uint32_t> next_timer() const override { return timer; }
	Scene::RepaintSurfaces timer_repaints() const override { return timer_repaints_; }

	/// The local row `render_oled` draws its Main rule on.
	static constexpr int32_t kMainLineY = 10;
	/// The local corner `render_oled` draws its Aux box at.
	static constexpr int32_t kAuxRectX = 4, kAuxRectY = 4;

	/// The colour `render_grid` paints every pad.
	RGB colour = kBaseColour;
	int oled_renders = 0, grid_renders = 0, events = 0, ticks = 0, model_changed = 0;
	int after_renders = 0, events_at_after_render = -1, events_at_render = -1, turns_seen = 0;
	/// Presses `render_grid` saw through its frame.
	int pads_pressed = 0;
	/// Whether the next `after_render()` / `tick()` raises `Dirty::Oled` (one-shot each).
	bool raise_in_after_render = false, raise_in_tick = false;
	/// Whether `leds()` declares `kRecordLed` on the shared normal blink.
	bool shared_blink = false;
	/// Whether `owns()` claims the select-encoder turn for the render, as a menu's list does.
	bool owns_turn = false;
	/// One letter per event `handle()` saw, in order: `P` pad, `E` encoder, `B` anything else.
	std::string order;
	uint32_t frame_ms = 0;
	/// Whether `render_oled` reads (and claims) the select-encoder turn.
	bool read_turn = false;
	/// Whether the next `handle()` returns a `Popup` action.
	bool emit_popup = false;
	/// Whether the next `handle()` pushes the overlay scene (one-shot).
	bool push_overlay_from_handle = false;
	/// Whether the next `after_render()` returns `Repaint{Main}` (one-shot).
	bool emit_repaint_from_after_render = false;
	/// Whether the next `handle()` returns `Repaint{Main}` (one-shot), which is how a scene reacts
	/// to input it reads in `handle()`.
	bool emit_repaint_from_handle = false;
	UiEvent::Kind last_event_kind = UiEvent::Kind::AllReleased;
	std::optional<uint32_t> timer;
	/// What `timer_repaints()` answers. Spelled out rather than left default-constructed: a
	/// default-constructed `RepaintSurfaces` is `{false, false}` (so that a designated initializer
	/// cannot arm an unnamed surface), while the value this double must
	/// start at is what `Scene::timer_repaints()`'s un-overridden *body* returns -- both surfaces.
	Scene::RepaintSurfaces timer_repaints_{.oled = true, .grid = true};
};

/// @brief The scene stacked on top: a grid overlay by default, with every composition hook
///        settable so one class covers greyout, occlusion and OLED transparency.
class OverlayScene final : public Scene {
public:
	explicit OverlayScene(SceneKind k = kOverlay) : kind_(k) {}
	SceneKind kind() const override { return kind_; }

	void render_oled(SurfaceRole, deluge::ui::oled::OledFrame& frame) override {
		oled_renders++;
		frame.rect({kBoxX, kBoxY, 30, 12});
	}
	void render_grid(deluge::ui::grid::GridFrame& frame) override {
		grid_renders++;
		if (grid_renders == 1) {
			first_frame_shift = frame.shift();
		}
		if (paints) {
			(void)frame.pad(0, 0, kOverlayColour);
		}
	}
	void leds(deluge::ui::LedFrame& frame) override {
		if (declares_play) {
			(void)frame.set(kPlayLed, deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, 200, 0});
		}
		if (shared_blink) {
			(void)frame.set(kSongLed, deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal));
		}
		if (restart_next) {
			restart_next = false;
			frame.restart(deluge::ui::BlinkRate::Normal);
		}
	}
	Result handle(const UiEvent&, const UiContext&) override {
		events++;
		return Result::unhandled();
	}
	void tick(uint32_t dt_ms) override {
		ticks++;
		ticked_ms += dt_ms;
	}

	bool is_grid_overlay() const override { return overlay; }
	bool passes_pads_beneath() const override { return passes_pads; }
	bool occludes_grid() const override { return occludes; }
	bool oled_transparent(SurfaceRole) const override { return transparent; }
	bool input_transparent() const override { return passive; }
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return mask; }

	/// The local corner `render_oled` draws its box at.
	static constexpr int32_t kBoxX = 60, kBoxY = 20;

	bool overlay = true, occludes = false, transparent = false;
	/// What `passes_pads_beneath()` answers.
	bool passes_pads = false;
	/// What `input_transparent()` answers: a notice-like layer that takes no input.
	bool passive = false;
	/// What the first `render_grid` read for shift.
	bool first_frame_shift = false;
	/// Whether `leds()` declares the play LED's 200 ms blink, `kSongLed` on the shared normal blink, and restarts the
	/// shared normal phase (one-shot).
	bool declares_play = true, shared_blink = false, restart_next = false;
	/// Whether `render_grid` paints its one pad.
	bool paints = true;
	deluge::ui::grid::GreyoutMask mask{};
	int oled_renders = 0, grid_renders = 0, events = 0, ticks = 0;
	/// The time every `tick()` has handed this scene, summed.
	uint32_t ticked_ms = 0;

private:
	SceneKind kind_;
};

/// @brief Build the registry table before it is handed to the `SceneRegistry` member, so the
///        `Shell` constructor -- which copies the table -- sees the scenes already in it.
std::array<Scene*, 19> table_for(Scene& base, Scene& overlay, Scene& upper) {
	std::array<Scene*, 19> t{};
	t[static_cast<size_t>(kBase)] = &base;
	t[static_cast<size_t>(kOverlay)] = &overlay;
	t[static_cast<size_t>(kUpper)] = &upper;
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
	OverlayScene upper{kUpper};
	std::array<Scene*, 19> table = table_for(base, overlay, upper);
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	Shell shell;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	explicit Rig(ShellConfig config = ShellConfig{}) : shell{fake, profile, reg, Services{}, config} {}

	/// @brief What `Shell::flush` does at the end of every tick: lower the pending
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

	it("an overlay composites over the base and leaves the rest as the base painted it", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();

		r.shell.tick(0);
		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		expect(g.at(0, 0) == kOverlayColour).to_be_true();
		expect(g.at(1, 0) == kBaseColour).to_be_true();
		expect(g.at(17, 7) == kBaseColour).to_be_true();
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
		RGB base = kBaseColour;
		const RGB greyed = base.greyOut(kGreyoutAmountFull);
		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		expect(g.at(0, 1) == greyed).to_be_true();
		expect(g.at(1, 1) == kBaseColour).to_be_true();
		// Greyout runs last, so even the overlay's own pad is tinted.
		RGB overlay_colour = kOverlayColour;
		expect(g.at(0, 0) == overlay_colour.greyOut(kGreyoutAmountFull)).to_be_true();
	});

	it("greys the grid by the topmost non-empty mask, not the union", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.colour = RGB{100, 100, 100};
		// Two overlays that paint nothing, so the greyout is the only thing on the grid:
		// a menu's mask under a keyboard's, which legacy's top-down `getUIGreyoutColsAndRows`
		// (`gui/ui/ui.cpp`) resolves to the keyboard's alone.
		r.overlay.paints = false;
		r.overlay.mask = deluge::ui::grid::GreyoutMask{0xFFFFFFFEu, 0};
		r.upper.paints = false;
		r.upper.mask = deluge::ui::grid::GreyoutMask{0b10u, 0};
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		expect(r.shell.stack().push({kUpper, 0})).to_be_true();
		r.shell.tick(0);

		expect(r.shell.greyout().cols).to_equal(uint32_t(0b10));
		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		// `cols` bit 1 is column `17 - 1` = 16.
		for (uint8_t y = 0; y < g.dims().rows; y++) {
			for (uint8_t x = 0; x < g.dims().cols(); x++) {
				expect(g.at(x, y) != RGB{100, 100, 100}).to_equal(x == 16);
			}
		}
	});

	it("greys_in_send_path leaves the composed grid ungreyed", _ {
		deluge::notify::dirty().drain();
		Rig r{ShellConfig{.greys_in_send_path = true}};
		r.base.colour = RGB{100, 100, 100};
		r.overlay.paints = false;
		r.overlay.mask = deluge::ui::grid::GreyoutMask{0xFFFFFFFEu, 0};
		r.upper.paints = false;
		r.upper.mask = deluge::ui::grid::GreyoutMask{0b10u, 0};
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		expect(r.shell.stack().push({kUpper, 0})).to_be_true();
		r.shell.tick(0);

		// The host still reads the mask; it is only the shell's own pass that is skipped.
		expect(r.shell.greyout().cols).to_equal(uint32_t(0b10));
		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		const auto ungreyed = [&g] {
			for (uint8_t y = 0; y < g.dims().rows; y++) {
				for (uint8_t x = 0; x < g.dims().cols(); x++) {
					if (g.at(x, y) != RGB{100, 100, 100}) {
						return false;
					}
				}
			}
			return true;
		};
		expect(ungreyed()).to_be_true();

		// Nor does the greyout ramp grey it, midway or settled.
		r.shell.start_tween(ActiveTween{ActiveTween::Kind::GreyOut, 0, 200, kGreyoutAmountFull});
		r.shell.tick(100);
		expect(ungreyed()).to_be_true();
		r.shell.tick(250);
		expect(ungreyed()).to_be_true();
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

	it("reconsider_wake brings the wake forward to a scene timer set since the tick, never later", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.base.timer = 50;
		r.shell.tick(0);
		expect(*r.shell.next_wake()).to_equal(uint32_t(50));
		// The host hands the scene an answer between ticks, which moves its timer.
		r.base.timer = 0;
		r.shell.reconsider_wake(5);
		expect(*r.shell.next_wake()).to_equal(uint32_t(5));
		r.base.timer = 100;
		r.shell.reconsider_wake(6);
		expect(*r.shell.next_wake()).to_equal(uint32_t(5));
	});

	it("a scene timer repaints every surface the scene is on, with no gate arm of its own", _ {
		// `BaseScene` arms nothing from `render_*`. Without the fold `next_timer()` would be a bare
		// wake: the shell would come back, `RepaintGate::needs_frame` would say no on every surface,
		// and the scene's state would advance against a framebuffer that never changed. So step 8b
		// of `Shell::tick` folds the two together.
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
		r.base.timer_repaints_ = Scene::RepaintSurfaces{.oled = false, .grid = false};
		r.shell.tick(20);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(40)); // the wake is kept...
		r.shell.tick(40);
		expect(r.base.ticks).to_be_greater_than(0);
		expect(r.base.oled_renders).to_equal(oled_after_first); // ...and nothing was drawn for it
		expect(r.base.grid_renders).to_equal(grid_after_first);
	});

	it("timer_repaints() can name the grid alone, and the OLED gates are never armed", _ {
		// The regression the first cut of the fold introduced: `Shell::flush` diffs the grid
		// against `last_sent` and blits nothing when a repaint changed no pixel, but it blits an
		// OLED surface unconditionally once it has rendered. `MenuScene::timer_repaints()` returns
		// `{.oled = false, .grid = true}` for exactly this reason -- its timer is the shortcut/
		// source pad blink cadence, a grid-only concern -- and this pins the mechanism directly:
		// a scene that names only the grid must see its OLED render count stay flat forever, no
		// matter how many quiet ticks land on the timer's cadence, while the grid keeps repainting.
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		const int oled_after_first = r.base.oled_renders;
		const int grid_after_first = r.base.grid_renders;

		r.base.timer = 20;
		r.base.timer_repaints_ = Scene::RepaintSurfaces{.oled = false, .grid = true};
		r.shell.tick(20);
		r.shell.tick(40);
		expect(r.base.grid_renders).to_equal(grid_after_first + 1);
		expect(r.base.oled_renders).to_equal(oled_after_first); // still flat

		// Several more cycles of the same cadence: the grid keeps moving, the OLED panels never do.
		r.shell.tick(60);
		r.shell.tick(80);
		r.shell.tick(100);
		expect(r.base.grid_renders).to_equal(grid_after_first + 4);
		expect(r.base.oled_renders).to_equal(oled_after_first);
	});

	it("a covered scene arms no grid gate, however loudly its timer claims one", _ {
		// `Shell::render_grid` draws from the grid BASE up, so a scene underneath a non-overlay
		// scene cannot paint the grid at all; step 8b must not arm from every stacked scene. Were it to,
		// a covered `MenuScene` whose focused node has source pads -- exactly the node that opens
		// Browser/Marker/Recorder -- would keep the grid gate firing at 20 ms for the whole life of the
		// scene above it. (An arm made inside `MenuScene::render_grid` simply stops being called
		// when covered; the fold has to match.)
		deluge::notify::dirty().drain();
		Rig r;
		r.overlay.overlay = false;    // NOT a grid overlay, so it becomes the grid base itself
		r.overlay.transparent = true; // the OLED is not what this example is about
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.base.timer = 20;
		r.base.timer_repaints_ = Scene::RepaintSurfaces{.oled = false, .grid = true};
		r.shell.tick(0);
		const int grid_after_first = r.base.grid_renders;
		expect(grid_after_first).to_equal(0); // covered: it never drew the grid at all

		// The gate itself, not a proxy for it: nothing armed a deadline on a surface this scene
		// cannot appear on. (`request_repaint()` from the stack change sets a flag, not a deadline.)
		expect(r.fake.grid().gate.next_deadline().has_value()).to_be_false();

		// It still gets its wake, and its `tick(dt)` with it -- `Shell::tick_stack` advances every
		// scene down to the first `occludes_grid()` whatever the render pass decides, so the
		// deadline moves to `compute_next_wake`'s own loop rather than disappearing.
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(20));
		const int ticks_after_first = r.base.ticks;
		r.shell.tick(20);
		r.shell.tick(40);
		r.shell.tick(60);
		expect(r.base.ticks).to_equal(ticks_after_first + 3);
		expect(r.base.grid_renders).to_equal(grid_after_first);
		expect(r.fake.grid().gate.next_deadline().has_value()).to_be_false();
	});

	it("a covered OLED timer under an opaque popup arms no panel gate and costs no blit", _ {
		// `RecorderScene` takes the default `{oled, grid}` claim with a 10 ms poll, and its own
		// clipping `PopupScene` is opaque on the OLED -- so were step 8b to arm every OLED gate every
		// 10 ms while that popup is up, `flush` (which blits an OLED surface unconditionally once
		// rendered) would blit at 100 Hz on both panels for the popup's lifetime, with no pixel ever
		// changing. The
		// per-surface mask cannot catch this one -- the surface is the right surface, the scene is
		// just not the drawn one.
		deluge::notify::dirty().drain();
		Rig r;
		r.overlay.overlay = true;      // a grid overlay, as `PopupScene` is
		r.overlay.transparent = false; // and opaque on the OLED, as `PopupScene` is
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.base.timer = 10;
		r.base.timer_repaints_ = Scene::RepaintSurfaces{.oled = true, .grid = false};
		r.shell.tick(0);
		const size_t blits_after_first = r.fake.blits.size();
		expect(r.base.oled_renders).to_equal(0); // covered on both panels

		expect(r.fake.oled()[0].gate.next_deadline().has_value()).to_be_false();
		expect(r.fake.oled()[1].gate.next_deadline().has_value()).to_be_false();
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(10)); // the wake survives

		// Ten poll periods of the popup's lifetime: not one blit, not one render.
		for (uint32_t t = 10; t <= 100; t += 10) {
			r.shell.tick(t);
		}
		expect(r.fake.blits.size()).to_equal(blits_after_first);
		expect(r.base.oled_renders).to_equal(0);
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

	it("two scenes' shared blinks of one rate blink together, and a restart by either restarts both", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.shared_blink = true;
		r.overlay.shared_blink = true;
		r.overlay.declares_play = false;
		r.shell.init({kBase, 0});
		r.shell.tick(0); // the base's blink starts here: lit 250 ms
		const size_t rec = static_cast<size_t>(static_cast<uint8_t>(kRecordLed));
		const size_t song = static_cast<size_t>(static_cast<uint8_t>(kSongLed));
		expect(r.fake.leds().desired_on[rec]).to_be_true();
		r.shell.tick(260);
		expect(r.fake.leds().desired_on[rec]).to_be_false();
		// The overlay's blink starts with a `blinkLed`, which restarts the base's too: both lit.
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.shell.tick(270);
		expect(r.fake.leds().desired_on[rec]).to_be_true();
		expect(r.fake.leds().desired_on[song]).to_be_true();
		r.shell.tick(530);
		expect(r.fake.leds().desired_on[rec]).to_be_false();
		expect(r.fake.leds().desired_on[song]).to_be_false();
		// And an explicit restart while both blink.
		r.overlay.restart_next = true;
		r.shell.tick(540);
		expect(r.fake.leds().desired_on[rec]).to_be_true();
		expect(r.fake.leds().desired_on[song]).to_be_true();
		r.shell.tick(800);
		expect(r.fake.leds().desired_on[rec]).to_be_false();
		expect(r.fake.leds().desired_on[song]).to_be_false();
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
		// And it is left exactly as the base painted it: legacy's popups have no grid effect at all.
		const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
		expect(g.at(3, 3) == kBaseColour).to_be_true();
		expect(g.at(17, 7) == kBaseColour).to_be_true();
	});

	it("a passive overlay leaves the scene beneath focused and routed", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.owns_turn = true;
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
		// ...and the router reached the base's handle(), not the popup's, on the next tick: the Play came after a
		// turn the base's widgets owned.
		r.shell.tick(31);
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

	it("an owned event is consumed by the render and not routed", _ {
		deluge::notify::dirty().drain();
		ownership_audit() = {};
		Rig r;
		r.base.owns_turn = true;
		r.base.read_turn = true;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.shell.on_encoder_edges(3, 2, 25);
		r.shell.tick(30);
		expect(r.base.turns_seen).to_equal(1);
		expect(r.base.events).to_equal(0); // the frame owned it
		expect(ownership_audit().undeclared_claims).to_equal(uint32_t(0));
		expect(ownership_audit().unclaimed_owned).to_equal(uint32_t(0));
	});

	it("an unowned event is routed before the render, which cannot read it", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.owns_turn = false;
		r.base.read_turn = true;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.shell.on_encoder_edges(3, 2, 25);
		r.shell.tick(30);
		expect(r.base.events).to_equal(1);
		expect(r.base.events_at_render).to_equal(1);
		expect(r.base.turns_seen).to_equal(0);
	});

	it("no event is delivered after the render", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.owns_turn = true;
		r.base.read_turn = false;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.shell.on_board_event(button_ev(8, 3, true), 25); // Play
		r.shell.on_encoder_edges(3, 2, 26);
		r.shell.tick(30);
		expect(r.base.events).to_equal(1); // Play only
		expect(r.base.events_at_after_render).to_equal(r.base.events);
	});

	it("counts a widget claiming what its scene said it does not own, and an owned event nothing claimed", _ {
		deluge::notify::dirty().drain();
		ownership_audit() = {};
		Rig denied;
		denied.base.owns_turn = false;
		denied.base.read_turn = true;
		denied.shell.init({kBase, 0});
		denied.shell.tick(0);
		denied.shell.on_encoder_edges(3, 2, 25);
		denied.shell.tick(30);
		expect(ownership_audit().undeclared_claims).to_equal(uint32_t(1));

		ownership_audit() = {};
		deluge::notify::dirty().drain();
		Rig unread;
		unread.base.owns_turn = true;
		unread.base.read_turn = false;
		unread.shell.init({kBase, 0});
		unread.shell.tick(0);
		unread.shell.on_encoder_edges(3, 2, 25);
		unread.shell.tick(30);
		expect(ownership_audit().unclaimed_owned).to_equal(uint32_t(1));
		ownership_audit() = {};
	});

	it("a widget-owned shape the scene does not own is routed before the render", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.emit_repaint_from_handle = true;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.consume_pending();

		r.shell.on_encoder_edges(3, 2, 25);
		r.shell.tick(30);
		expect(r.base.events).to_equal(1);
		expect(r.base.events_at_render).to_equal(1); // handled before this tick's frame
		expect(r.base.events_at_after_render).to_equal(1);
		// The Repaint it asked for was drawn this tick, so nothing is left waiting.
		expect(r.shell.next_wake().has_value() && *r.shell.next_wake() == 30).to_be_false();
	});

	it("events reach the scene in arrival order", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.shell.on_encoder_edges(3, 2, 25);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 2, 2, 64}, 26);
		r.shell.tick(30);
		expect(r.base.order).to_equal(std::string("EP"));
	});

	it("a fact raised in after_render is drawn on the next tick without input", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.consume_pending();
		r.base.raise_in_after_render = true;
		deluge::notify::dirty().raise(Dirty::Oled); // a reason to render, so after_render runs
		const int changed = r.base.model_changed;
		r.shell.tick(10);
		// Step 1's fact, then the one after_render raised, at the end of the same tick.
		expect(r.base.model_changed).to_equal(changed + 2);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(10));
		const int renders = r.base.oled_renders;
		r.consume_pending();
		r.shell.tick(20);
		expect(r.base.oled_renders).to_be_greater_than(renders);
	});

	it("a fact raised in tick_stack is drawn on the next tick without input", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.consume_pending();
		r.base.raise_in_tick = true;
		r.shell.tick(10);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(10));
		const int renders = r.base.oled_renders;
		r.consume_pending();
		r.shell.tick(20);
		expect(r.base.oled_renders).to_be_greater_than(renders);
	});

	it("a fact raised between ticks is a wake, whatever the legacy drain took", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		expect(r.shell.next_wake().has_value()).to_be_false();
		deluge::notify::dirty().raise(Dirty::Oled);
		(void)deluge::notify::dirty().drain(deluge::notify::DirtyConsumer::Legacy);
		expect(r.shell.next_wake(5).has_value()).to_be_true();
		expect(*r.shell.next_wake(5)).to_equal(uint32_t(5));
		const int changed = r.base.model_changed;
		r.shell.tick(5);
		expect(r.base.model_changed).to_equal(changed + 1);
	});

	it("a scene covered by an overlay that passes nothing reads no pads", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.shell.tick(0);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 2, 2, 64}, 25);
		r.shell.tick(30);
		expect(r.base.grid_renders).to_be_greater_than(1);
		expect(r.base.pads_pressed).to_equal(0);
	});

	it("a scene covered by an overlay that passes pads beneath reads them", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.overlay.passes_pads = true;
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.shell.tick(0);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 2, 2, 64}, 25);
		r.shell.tick(30);
		expect(r.base.pads_pressed).to_equal(1);
	});

	it("an event after one the scene's widgets own waits for the next tick, which comes at once", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.base.owns_turn = true;
		r.base.read_turn = true;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.shell.on_encoder_edges(3, 2, 25);
		r.shell.on_board_event(button_ev(8, 3, true), 26); // Play, after the owned turn
		r.shell.tick(30);
		expect(r.base.turns_seen).to_equal(1);
		expect(r.base.events).to_equal(0); // not routed against a scene that has not drawn the turn yet
		expect(*r.shell.next_wake()).to_equal(uint32_t(30));
		r.shell.tick(31);
		expect(r.base.events).to_equal(1);
		expect(r.base.turns_seen).to_equal(1); // the turn was not carried twice
	});

	it("a scene that owns input under an opaque passive overlay still draws, and claims it", _ {
		deluge::notify::dirty().drain();
		ownership_audit() = {};
		Rig r;
		r.base.owns_turn = true;
		r.base.read_turn = true;
		r.overlay.passive = true;
		r.shell.init({kBase, 0});
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true();
		r.shell.tick(0);
		r.shell.on_encoder_edges(3, 2, 25);
		r.shell.tick(30);
		expect(r.base.turns_seen).to_equal(1);
		expect(ownership_audit().unclaimed_owned).to_equal(uint32_t(0));
	});

	it("a scene pushed between ticks starts its clock at the next tick", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		expect(r.shell.stack().push({kOverlay, 0})).to_be_true(); // a host's push, as ModKnobs' popup is
		r.shell.tick(1000);
		expect(r.overlay.ticked_ms).to_equal(uint32_t(0));
		r.shell.tick(1010);
		expect(r.overlay.ticked_ms).to_equal(uint32_t(10));
	});

	it("a scene a pad press pushes reads shift on its first frame", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.shell.on_board_event(button_ev(8, 0, true), 5); // Shift down
		r.shell.tick(10);
		r.base.push_overlay_from_handle = true;
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 2, 2, 64}, 15);
		r.shell.tick(20);
		expect(r.shell.stack().top().kind() == kOverlay).to_be_true();
		expect(r.overlay.first_frame_shift).to_be_true();
	});

	it("a scene pushed by this tick's input starts its clock at this tick", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		r.base.push_overlay_from_handle = true;
		r.shell.on_board_event(button_ev(8, 3, true), 25);
		r.shell.tick(30);
		expect(r.shell.stack().top().kind() == kOverlay).to_be_true();
		expect(r.overlay.ticks).to_equal(1);
		expect(r.overlay.ticked_ms).to_equal(uint32_t(0)); // it was not up for the 30 ms this tick covers
		r.shell.tick(40);
		expect(r.overlay.ticked_ms).to_equal(uint32_t(10));
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

	it("events deferred by the SD lock are routed once the lock is released", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.shell.set_sd_busy(true);
		// Back, at matrix (7, 1). The replay has to land in an open window: `begin_tick` clears the
		// press edges and `had_input_this_tick`, so a replay into a closed one would be drawn by
		// nothing.
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

	it("a second popup over a live one reaches the panel on a tick with no input", _ {
		deluge::notify::dirty().drain();
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		deluge::notify::notice_text("FIRST");
		r.shell.tick(10);
		const std::string first = r.fake.pbm(0);
		r.consume_pending();
		deluge::notify::notice_text("SECOND");
		r.shell.tick(20); // quiet: no pad, button or encoder, only the drain
		expect(r.fake.pbm(0) != first).to_be_true();
	});

	it("drains the notice queue only while it owns it", _ {
		deluge::notify::dirty().drain();
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		expect(r.shell.owns_notices()).to_be_true();
		r.shell.set_owns_notices(false);
		deluge::notify::notice_text("SAVED");
		r.shell.tick(10);
		expect(deluge::notify::notices().size()).to_equal(size_t{1});
		expect(r.shell.stack().top().kind() == SceneKind::Popup).to_be_false();
		r.shell.set_owns_notices(true);
		r.shell.tick(20);
		expect(deluge::notify::notices().size()).to_equal(size_t{0});
		expect(r.shell.stack().top().kind() == SceneKind::Popup).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(shell_tick_spec, shell_tick)
