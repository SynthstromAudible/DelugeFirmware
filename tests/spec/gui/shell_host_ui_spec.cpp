#include "support/fake_legacy_hid.h"
#include "support/fake_legacy_ui.h"

#include "gui/ui/shell_host_ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/button.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/settings/runtime_feature_settings.h"
#include "shell/profiles/deluge.h"
#include "storage/flash_storage.h"
#include "ui/core/style.h"

#include "cppspec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "../support/spec_main.h"

using deluge::gui::ui::ShellHostUI;
using deluge::hid::display::OLED;
using namespace deluge::ui::shell;

namespace {

constexpr SceneKind kBase = SceneKind::FirstApp;
constexpr SceneKind kOver = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 1);

/// @brief Some other `UI`, for the one example that needs `getCurrentUI()` to answer "not the
///        guest".
///
/// A real `UI` rather than a cast pointer: `rearmTimer`'s guard only ever compares the address, but
/// an example that lies about a type to make a comparison come out right is one nobody can later
/// change safely. `UI` has exactly two pure virtuals (`gui/ui/ui.h:157`, `:164`), and neither is
/// reached here -- nothing renders this and nothing asks its type.
class OtherUI final : public UI {
public:
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

/// @brief A scene that paints a colour derived from (x, y), so a copy that transposes, offsets or
///        drops a column is visible rather than merely "different".
///
/// Every composition answer is a settable member: the two the guest maps
/// (`is_grid_overlay` -> `renderMainPads`'s return, `occludes_grid` -> `canSeeViewUnderneath`) plus
/// `greyout_mask`, so one scene type can stand for both a menu-shaped overlay and a full-screen
/// root without the fixture varying anything else at the same time.
class PatternScene final : public Scene {
public:
	explicit PatternScene(SceneKind k) : kind_(k) {}

	SceneKind kind() const override { return kind_; }

	void render_oled(SurfaceRole, deluge::ui::oled::OledFrame& frame) override {
		// Three lines at different lengths: enough that a frame served from the wrong surface, or
		// half-served, is not a plausible match.
		frame.hline(3, 0, 100);
		frame.hline(9, 4, 40);
		frame.hline(20, 10, 60);
	}

	void render_grid(deluge::ui::grid::GridFrame& frame) override {
		const deluge::gfx::grid::Rect b = frame.bounds();
		for (int32_t y = 0; y < b.h; y++) {
			for (int32_t x = 0; x < b.w; x++) {
				(void)frame.pad(x, y, pad_for(x, y));
			}
		}
	}

	/// @brief One routed event, with the parts of the `UiContext` that came with it.
	///
	/// The context is captured rather than the reference kept: `UiContext` is an aggregate of
	/// borrowed references built on the stack per event (`shell/scene.h:160-162`) and does not
	/// outlive the `handle()` call.
	struct Seen {
		UiEvent ev;
		bool shift;
		bool sticky;
		uint32_t now_ms;
	};

	Result handle(const UiEvent& ev, const UiContext& ctx) override {
		seen.push_back(Seen{ev, ctx.shift, ctx.sticky, ctx.now_ms});
		if (remove_self_on_event) {
			// `MenuScene`'s answer to Back at depth 0 (`scenes/menu/menu_scene.cpp:1684-1689`),
			// reduced to the one thing this suite is about: a scene that takes itself off the
			// stack from inside `Shell::tick`'s routing. Off by default so every other example
			// keeps the delivery-only behaviour below.
			Result r;
			r.handled = true;
			(void)r.actions.push_back(
			    deluge::ui::shell::Action{.kind = deluge::ui::shell::Action::Kind::Remove, .pop_to = kind_});
			return r;
		}
		// Unhandled on purpose: the guest's input hooks are about *delivery*, and a scene that
		// claimed an event would stop it reaching anything below it and make the routing this
		// records depend on what the scene decided.
		return Result::unhandled();
	}

	/// Make `handle` take this scene off the stack, the way `MenuScene` answers Back at depth 0.
	bool remove_self_on_event = false;

	/// Every event routed to this scene, in order.
	std::vector<Seen> seen;

	/// @brief Declare `led` for this frame, if `led_declared`.
	///
	/// The whole of the guest's LED story runs through here: `Shell::derive_leds` composes this
	/// frame, `Shell::flush` diffs it and calls `SurfaceSet::set_led` only for an id it holds
	/// (`shell/leds.cpp:75-80`), and `GuestSurfaceSet::set_led` turns that into
	/// `indicator_leds::setLedState`. Settable rather than fixed so one scene can be asked both
	/// halves of the ownership rule -- declaring an LED, and then not.
	void leds(deluge::ui::LedFrame& frame) override {
		if (led_declared) {
			(void)frame.set(led_id, led_state);
		}
	}

	std::optional<uint32_t> next_timer() const override { return timer; }

	bool is_grid_overlay() const override { return overlay; }
	bool occludes_grid() const override { return occludes; }
	bool dims_grid() const override { return false; }
	bool input_transparent() const override { return passive; }
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return greyout; }
	GridPadClaim grid_pad_claim() const override { return claim; }

	void on_resume() override { resumes++; }
	void on_exit() override { exits++; }
	bool on_enter(const SceneId&) override {
		enters++;
		return true;
	}

	/// @brief The colour this scene paints at (x, y) -- the spec's own sentinel, computed from the
	///        coordinates rather than read back out of anything the guest touched.
	static RGB pad_for(int32_t x, int32_t y) {
		return RGB{static_cast<uint8_t>(1 + x * 7), static_cast<uint8_t>(1 + y * 13),
		           static_cast<uint8_t>(1 + x + y * 3)};
	}

	bool overlay = false;
	bool occludes = false;
	bool passive = false;
	/// What `grid_pad_claim()` answers. `GridPadClaim::All` -- `Scene`'s own default -- unless an
	/// example wants `ShellHostUI::claimsGridPad` to see something else.
	GridPadClaim claim = GridPadClaim::All;
	bool led_declared = false;
	deluge::ui::LedId led_id{};
	deluge::ui::LedState led_state{};
	/// What `next_timer()` answers. `std::nullopt` -- `Scene`'s own default -- unless an example
	/// wants a deadline.
	std::optional<uint32_t> timer{};
	deluge::ui::grid::GreyoutMask greyout{};
	int enters = 0;
	int exits = 0;
	int resumes = 0;

private:
	SceneKind kind_;
};

/// @brief The 64-row panel every drawing spec in this stack is also checked at.
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief A `ShellHostUI` over a two-entry table, with both sets of legacy fakes reset.
/// @brief A `MIDICable` **address**, and deliberately nothing more.
///
/// The two learn hooks take `MIDICable&` and this suite cannot build one: `MIDICable` is abstract
/// and every shipped subclass needs a USB or DIN backend behind it
/// (`sim/scenes_spec/midi_learn_spec.cpp`'s `SpecCable` makes the same observation, in a target
/// that does link `io/midi/`). It does not need to build one, because **nothing on this path reads
/// through the pointer**: `src/shell/**` may not include `io/`, so `UiEvent::cable` is a
/// forward-declared type the shell only ever copies (`shell/scene.h`), and the one thing in the
/// tree that dereferences it -- `services::MidiLearn` -- is in the closure this suite does not
/// build. So the examples below assert the identity of the address that came out against the one
/// that went in, which is the whole of what this layer transports.
alignas(alignof(std::max_align_t)) std::byte cable_storage[sizeof(void*) * 4]{};

MIDICable& fake_cable() {
	return *reinterpret_cast<MIDICable*>(&cable_storage[0]);
}

struct Rig {
	PatternScene base{kBase};
	PatternScene over{kOver};
	std::array<Scene*, 18> table{};
	SceneRegistry reg{std::span<Scene* const>{table}};
	ShellHostUI guest;

	Rig() : guest(fill(), kDelugeBoard, deluge_profile()) {
		fakelegacy::reset();
		fakelegacyui::reset();
		fakelegacyui::current_ui = &guest;
	}
	explicit Rig(const DelugeBoard& board, const BoardProfile& profile) : guest(fill(), board, profile) {
		fakelegacy::reset();
		fakelegacyui::reset();
		fakelegacyui::current_ui = &guest;
	}
	/// Never leave `getCurrentUI()` pointing at a destroyed guest. Only ever compared, never
	/// dereferenced -- but a dangling pointer that is only compared is still a dangling pointer.
	~Rig() { fakelegacyui::current_ui = nullptr; }
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// Fill the table and hand back the registry -- runs before `guest`'s constructor reads it,
	/// which is why it is a member function call in the initialiser rather than a body statement.
	const SceneRegistry& fill() {
		table[static_cast<size_t>(kBase)] = &base;
		table[static_cast<size_t>(kOver)] = &over;
		return reg;
	}

	bool open(SceneKind k = kBase) {
		guest.setInitialScene(SceneId{k, 0});
		return guest.opened();
	}
};

/// @brief A board whose pad grid is **taller and narrower** than legacy's, with legacy's own panel.
///
/// `SurfaceSet::build` sizes the grid from `DelugeBoard::pad_grid_width`/`pad_grid_height` and
/// `BoardProfile::sidebar_cols` (`shell/surfaces.cpp:26-28`) and never consults legacy's numbers, so
/// nothing but `serveGrid`'s own two `min`s keeps the copy inside
/// `PadLEDs::image[kDisplayHeight][kDisplayWidth + kSideBarWidth]`. `canOpen()` cannot help: it is
/// the *panel's* shape, which this board leaves alone.
///
/// One board exercises both clamps, in opposite directions:
///   * **12 rows** (legacy has 8) — without `min(dims.rows, kDisplayHeight)` the copy writes four
///     rows past the end of the image it was handed.
///   * **8 main + 2 sidebar = 10 columns** (legacy has 18) — without `min(x1, dims.cols())` the copy
///     reads `PadGrid::at(10..15, y)`, which is a `DELUGE_GFX_CONTRACT` violation and traps
///     (`gfx/contract.h:34-39`, `gfx/grid/pad_grid.h:60-75`).
DelugeBoard board_tall_narrow_grid() {
	DelugeBoard b = kDelugeBoard;
	b.pad_grid_width = 8;
	b.pad_grid_height = 12;
	return b;
}

/// @brief A profile whose encoder table is in a **different order** from the Deluge's.
///
/// `ShellHostUI::feedDetents` resolves a role to a board index through
/// `BoardProfile::encoder_by_index` rather than hard-coding one; this is what makes that
/// resolution assertable instead of accidentally right. The Deluge's order is
/// Vertical, Horizontal, Tempo, Select, GoldUpper, GoldLower (`shell/profiles/deluge.cpp:69-77`);
/// this reverses the first four, so an implementation that hard-coded index 1 for Horizontal would
/// feed Select instead.
const BoardProfile& profile_permuted_encoders() {
	static const BoardProfile p = [] {
		static constexpr deluge::ui::EncoderRole kOrder[] = {
		    deluge::ui::EncoderRole::Select,     // 0
		    deluge::ui::EncoderRole::Tempo,      // 1
		    deluge::ui::EncoderRole::Horizontal, // 2
		    deluge::ui::EncoderRole::Vertical,   // 3
		    deluge::ui::EncoderRole::GoldUpper,  // 4
		    deluge::ui::EncoderRole::GoldLower,  // 5
		};
		BoardProfile q = deluge_profile();
		q.encoder_by_index = kOrder;
		return q;
	}();
	return p;
}

/// @brief A profile with **no** horizontal encoder, so the "role nothing carries" arm is drivable.
const BoardProfile& profile_no_horizontal() {
	static const BoardProfile p = [] {
		static constexpr deluge::ui::EncoderRole kOrder[] = {
		    deluge::ui::EncoderRole::Vertical,
		    deluge::ui::EncoderRole::Tempo,
		    deluge::ui::EncoderRole::Select,
		};
		BoardProfile q = deluge_profile();
		q.encoder_by_index = kOrder;
		return q;
	}();
	return p;
}

/// @brief Zero legacy's pad image without touching anything else, so a later read of it can only be
///        something the guest wrote.
void blank_legacy_pads() {
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			PadLEDs::image[y][x] = RGB{0, 0, 0};
		}
	}
}

/// @brief Is any byte of the canvas set? A frame comparison between two blank panels passes
///        forever; every render example here asserts this too.
bool any_ink(std::span<const uint8_t> bytes) {
	for (uint8_t b : bytes) {
		if (b != 0) {
			return true;
		}
	}
	return false;
}

std::vector<uint8_t> copy_of(std::span<const uint8_t> bytes) {
	return std::vector<uint8_t>{bytes.begin(), bytes.end()};
}

/// @brief The first event of \p kind in \p scene, or null.
///
/// A feed usually leaves more than one event in the queue -- a shift press and the pad it modifies,
/// a replayed deferral and the press that released the lock -- so an example that means "the pad"
/// has to say so rather than index blindly.
const PatternScene::Seen* first_of(const PatternScene& scene, UiEvent::Kind kind) {
	for (const PatternScene::Seen& s : scene.seen) {
		if (s.ev.kind == kind) {
			return &s;
		}
	}
	return nullptr;
}

/// @brief A `Service` that declares one knob indicator, standing in for `ModKnobs`.
///
/// `Shell::flush` calls `SurfaceSet::set_indicator` only for an indicator declared this tick
/// (`shell/shell.cpp:1032-1044`), and the only declarer in the shipped tree is
/// `scenes::services::ModKnobs` (`scenes/services/mod_knobs.cpp:687`) -- a *service*, which F1 task 8
/// installs and this guest does not have. This is the smallest thing that stands where `ModKnobs`
/// will stand, so "the guest writes no indicator" can be shown to be about the declaration rather
/// than about the guest.
class IndicatorService final : public Service {
public:
	void leds(deluge::ui::LedFrame& frame) override {
		(void)frame.indicator(deluge::ui::IndicatorId::Lower, deluge::ui::KnobIndicator{{0x11, 0x22, 0x5A, 0x77}});
	}
};

/// @brief Put the guest's clock at \p ms.
///
/// `ShellHostUI::nowMs()` is `getSystemTime() * 1000` truncated to an integer, and `getSystemTime()`
/// is seconds. The extra quarter-millisecond is not slop: `ms / 1000.0 * 1000.0` is not guaranteed
/// to land back on `ms` in a double, and one ulp low truncates to `ms - 1`. A quarter of a
/// millisecond is far below anything this file asserts about and far above that error.
void set_clock_ms(uint32_t ms) {
	fakelegacyui::system_time_s = (static_cast<double>(ms) + 0.25) / 1000.0;
}

/// @brief Run the guest's own timer chain for \p rounds callbacks, with **no input at all**.
///
/// Nothing here decides when the guest should run. Each round reads the duration the guest last
/// asked `uiTimerManager` for, advances the clock by exactly that, clears the timer the way
/// `UITimerManager::routine` does before it dispatches (`gui/ui_timer_manager.cpp:62-66`), and calls
/// `timerCallback()` the way that function's `UI_SPECIFIC` arm does (`:142-148`). A guest that
/// re-arms nothing ends the loop early -- which is what makes "the blink froze on an idle system" a
/// failed expectation rather than a slow test.
/// @return The clock each callback ran at, in order; shorter than \p rounds if the chain stopped.
std::vector<uint32_t> run_quiet(ShellHostUI& guest, uint32_t& now_ms, int rounds) {
	std::vector<uint32_t> fired;
	for (int i = 0; i < rounds; i++) {
		const int32_t in = fakelegacyui::ui_specific_armed_ms();
		if (in < 0) {
			break; // Nothing armed: the chain is over, and the caller's expectations say so.
		}
		now_ms += static_cast<uint32_t>(in);
		set_clock_ms(now_ms);
		uiTimerManager.getTimer(TimerName::UI_SPECIFIC).active = false;
		(void)guest.timerCallback();
		fired.push_back(now_ms);
	}
	return fired;
}

/// @brief `v[i]`, or a value no expectation here uses, when the sequence is shorter than that.
///
/// Every assertion below is about *which* tick fired, so a mutation that stops the chain early must
/// fail an expectation rather than index off the end of the vector -- an out-of-range read is
/// undefined behaviour, and `expect` does not abort the example the way an assert would.
template <typename T>
T nth_or(const std::vector<T>& v, size_t i, T fallback) {
	return i < v.size() ? v[i] : fallback;
}

/// @brief Every `indicator_leds::setLedState` call for \p board_led, as its on/off sequence.
std::vector<bool> led_history(uint8_t board_led) {
	std::vector<bool> out;
	for (const fakelegacy::LedWrite& w : fakelegacy::led_writes) {
		if (w.led == board_led) {
			out.push_back(w.on);
		}
	}
	return out;
}

} // namespace

// clang-format off
describe shell_host_ui("gui::ui::ShellHostUI", $ {

	// ---- lifecycle ----------------------------------------------------------------------------

	it("opens by pushing the scene it was given, and refuses when it was given none", _ {
		Rig r;
		expect(r.guest.canOpen()).to_be_true();
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(r.base.enters).to_equal(1);

		// A guest nobody called `setInitialScene` on holds a default `SceneId`, whose kind is
		// `SceneKind::Null` -- unregistered, so `SceneStack::push` refuses it and the stack stays
		// empty. An empty guest would swallow every pad while drawing nothing, so that is a refused
		// open, not an open guest.
		Rig r2;
		expect(r2.guest.getInitialScene().kind == SceneKind::Null).to_be_true();
		expect(r2.guest.opened()).to_be_false();
		expect(r2.guest.shell().stack().depth()).to_equal(size_t(0));
	});

	it("re-opening starts a fresh stack rather than stacking the same scene twice", _ {
		Rig r;
		expect(r.open()).to_be_true();
		// Something pushed over the menu, and then the guest is opened again without having been
		// unwound -- e.g. a refused rollback, or a caller that opens it twice.
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(2));

		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		// Both scenes were exited by the reset before the new push, so neither is stacked twice --
		// which `SceneStack` has no coherent lifecycle for (`shell/scene_stack.h:200-206`).
		expect(r.over.exits).to_equal(1);
		expect(r.base.exits).to_equal(1);
		expect(r.base.enters).to_equal(2);
	});

	it("resumes the input top on focusRegained, not a passive layer sitting above it", _ {
		Rig r;
		r.over.passive = true; // a popup / console line: drawn over, never taking input
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		r.base.resumes = 0;
		r.over.resumes = 0;

		r.guest.focusRegained();

		// `top()` is the passive layer; `input_top()` is the scene that genuinely gets input back,
		// and `Scene::on_resume`'s contract is about the latter (`shell/scene.h:359-374`).
		expect(r.guest.shell().stack().top().kind() == kOver).to_be_true();
		expect(r.base.resumes).to_equal(1);
		expect(r.over.resumes).to_equal(0);
	});

	it("refuses to open at a panel geometry legacy's canvas cannot hold, and draws nothing there", _ {
		Rig r{board_64(), profile_64()};
		// 128x64 at 1bpp is 1024 bytes; `OLED::main` has 768 and no way to grow.
		expect(r.guest.surfaces().oled()[0].geometry.height).to_equal(uint16_t(64));
		expect(r.guest.canOpen()).to_be_false();
		// The panel has to be the ONLY reason it refuses: `open()` gives it a scene that is really
		// registered, so a guest that skipped the `canOpen()` check would push it and return true.
		// (Without this the example passed for the wrong reason -- the un-set initial scene -- and
		// mutation M18, "drop the canOpen refusal", survived.)
		expect(r.open()).to_be_false();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		expect(r.base.enters).to_equal(0);

		// And `renderOLED` must leave the canvas legacy handed it exactly as it found it: a
		// half-copied frame is worse than the stale one legacy would otherwise send.
		OLED::main.clear();
		r.guest.renderOLED(OLED::main);
		expect(any_ink(OLED::main.bytes())).to_be_false();
	});

	it("refuses a panel that is the wrong SHAPE even when it is the right number of bytes", _ {
		// 64x96 is 768 bytes, exactly what `OLED::main` holds, so a byte-count check would say yes
		// and every frame would then be copied in garbled -- F1 task 4's review Important 4, in the
		// guest's own hook rather than in `blit`'s. This is the only board that separates the shape
		// guard from the size guard, which is why it is here and not folded into the 64-row example.
		static constexpr DelugeDisplayDesc kDisplays96[] = {{64, 96, 0}};
		DelugeBoard b = kDelugeBoard;
		b.display_width = 64;
		b.display_height = 96;
		b.display_count = 1;
		b.displays = kDisplays96;
		Rig r{b, deluge_profile()};

		expect(r.guest.surfaces().oled()[0].canvas.bytes().size()).to_equal(OLED::main.bytes().size());
		expect(r.guest.canOpen()).to_be_false();
		expect(r.open()).to_be_false();

		std::span<uint8_t> legacy = OLED::main.bytes();
		for (size_t i = 0; i < legacy.size(); i++) {
			legacy[i] = static_cast<uint8_t>(0x3C);
		}
		r.guest.renderOLED(OLED::main);
		for (uint8_t byte : OLED::main.bytes()) {
			expect(byte).to_equal(uint8_t(0x3C));
		}
	});

	// ---- the OLED, which is the hook legacy's own clear makes necessary -------------------------

	it("hands the frame back after legacy clears the panel under it, at the shipped 48-row panel", _ {
		Rig r;
		expect(r.open()).to_be_true();
		// One shell pass. `GuestSurfaceSet::blit` copies the rendered panel into `OLED::main` and
		// marks it changed; that copy is the oracle for what legacy is meant to end up holding.
		//
		// This is also the assertion that `opened()` does not need a `Shell::boot()` of its own
		// (F1 task 5 review round, Important 1): the *first* tick after opening must draw, and it
		// does, because `SceneStack::reset` and the push both set `changed_`
		// (`shell/scene_stack.cpp:201-202`, `:39`) and `Shell::tick` re-arms every gate on
		// `changed_since_last_query()` before it renders (`shell/shell.cpp:346-358`). The `boot()`
		// call that used to sit in `opened()` was removed and this stayed green, which is the
		// evidence it was dead.
		r.guest.shell().tick(0);
		const std::vector<uint8_t> rendered = copy_of(OLED::main.bytes());
		expect(any_ink(rendered)).to_be_true();
		expect(fakelegacy::consume_oled_frame()).to_be_true();

		// Now legacy's own render pass: `doAnyPendingOLEDRendering` clears the image
		// (`gui/ui/ui.cpp:358`) and then asks each open UI to draw into it (`:362`). Without this
		// hook the shell would have nothing armed to re-blit and the frame would go out blank.
		OLED::main.clear();
		expect(any_ink(OLED::main.bytes())).to_be_false();
		r.guest.renderOLED(OLED::main);
		expect(copy_of(OLED::main.bytes()) == rendered).to_be_true();

		// It restores, it does not re-mark: legacy has already decided to send this frame by the
		// time it calls here (`gui/ui/ui.cpp:365`).
		expect(fakelegacy::oled_marked()).to_be_false();
	});

	it("serves the frame from the shell's own Main surface, not from whatever legacy last held", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		const std::vector<uint8_t> shellSide = copy_of(r.guest.surfaces().oled()[0].canvas.bytes());
		expect(any_ink(shellSide)).to_be_true();

		// Put something else in legacy's buffer entirely, so a hook that "restored" by leaving the
		// buffer alone, or by copying it onto itself, cannot pass.
		std::span<uint8_t> legacy = OLED::main.bytes();
		for (size_t i = 0; i < legacy.size(); i++) {
			legacy[i] = static_cast<uint8_t>(0xA5);
		}
		r.guest.renderOLED(OLED::main);
		expect(copy_of(OLED::main.bytes()) == shellSide).to_be_true();
	});

	// ---- the pads -----------------------------------------------------------------------------

	it("serves the shell's grid into legacy's image row by row, main columns only", _ {
		Rig r;
		r.base.occludes = true; // a full-screen scene: the guest owns the pads
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		// The shell's own flush has already written the image through `send_columns`; blank it, so
		// what is read back afterwards can only be this hook's doing.
		blank_legacy_pads();

		const bool usedUp = r.guest.renderMainPads(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask, true);
		expect(usedUp).to_be_true();
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				expect(PadLEDs::image[y][x] == PatternScene::pad_for(x, y)).to_be_true();
			}
			// The sidebar is `renderSidebar`'s, and legacy asks the two questions separately.
			expect(PadLEDs::image[y][kDisplayWidth] == RGB{0, 0, 0}).to_be_true();
			expect(PadLEDs::image[y][kDisplayWidth + 1] == RGB{0, 0, 0}).to_be_true();
			// The occupancy map is untouched, and that is a stated gap rather than a feature:
			// nothing in the shell populates one (F1 task 4 recorded the same for `send_columns`),
			// so the guest leaves legacy's alone rather than inventing values for it.
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(PadLEDs::occupancyMask[y][x]).to_equal(uint8_t(0));
			}
		}
	});

	it("serves only the sidebar columns from renderSidebar", _ {
		Rig r;
		r.base.occludes = true;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		blank_legacy_pads();

		expect(r.guest.renderSidebar(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask)).to_be_true();
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			expect(PadLEDs::image[y][kDisplayWidth] == PatternScene::pad_for(kDisplayWidth, y)).to_be_true();
			expect(PadLEDs::image[y][kDisplayWidth + 1] == PatternScene::pad_for(kDisplayWidth + 1, y)).to_be_true();
			expect(PadLEDs::image[y][0] == RGB{0, 0, 0}).to_be_true();
			expect(PadLEDs::image[y][kDisplayWidth - 1] == RGB{0, 0, 0}).to_be_true();
		}
	});

	it("writes only the rows whichRows selects", _ {
		Rig r;
		r.base.occludes = true;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		blank_legacy_pads();

		(void)r.guest.renderMainPads(0b00000101, PadLEDs::image, PadLEDs::occupancyMask, true);
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			const bool asked = (y == 0 || y == 2);
			const RGB got = PadLEDs::image[y][3];
			expect(got == PatternScene::pad_for(3, y)).to_equal(asked);
			expect(got == RGB{0, 0, 0}).to_equal(!asked);
		}
	});

	it("stays inside legacy's image when the board's grid is taller, and inside the grid when it is "
	   "narrower", _ {
		// F1 task 5 review round, Important 1: both of `serveGrid`'s clamps survived mutation on the
		// first pass, because no example varied the grid's dimensions at all -- every board in the
		// file had legacy's own 16+2 x 8. Unlike `renderOLED`'s size guard (M28), these two are
		// reachable: `canOpen()` guards the panel's shape and says nothing about the grid.
		Rig r{board_tall_narrow_grid(), deluge_profile()};
		r.base.occludes = true;
		expect(r.guest.canOpen()).to_be_true(); // the panel is untouched; only the grid differs
		expect(r.open()).to_be_true();
		expect(r.guest.surfaces().grid().grid().dims().rows).to_equal(uint8_t(12));
		expect(r.guest.surfaces().grid().grid().dims().cols()).to_equal(uint16_t(10));
		r.guest.shell().tick(0);

		// Deliberately NOT `PadLEDs::image`. The row width is legacy's, so the guest cannot tell the
		// difference, but the buffer has `kMaxGridRows` rows instead of `kDisplayHeight` -- so a copy
		// that honoured the board's 12 rows instead of legacy's 8 lands in memory this example owns
		// and can assert about, rather than in whatever follows `image` in the fake's data segment.
		// (The suite's sanitizers are opt-in, `DELUGE_TEST_SANITIZERS` defaulting OFF in
		// `tests/CMakeLists.txt:23`, so an out-of-bounds write is otherwise silent here.)
		RGB oversized[deluge::gfx::grid::kMaxGridRows][kDisplayWidth + kSideBarWidth]{};

		const bool usedUp = r.guest.renderMainPads(0xFFFFFFFF, oversized, PadLEDs::occupancyMask, true);
		expect(usedUp).to_be_true();

		for (int32_t y = 0; y < kDisplayHeight; y++) {
			// Columns the board actually has: copied.
			for (int32_t x = 0; x < 10; x++) {
				expect(oversized[y][x] == PatternScene::pad_for(x, y)).to_be_true();
			}
			// Columns it does not: left alone, rather than read out of the grid's range.
			for (int32_t x = 10; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(oversized[y][x] == RGB{0, 0, 0}).to_be_true();
			}
		}
		// Rows past legacy's eight: never written, however many rows the board's grid has. This is
		// the assertion that would have caught a write past the end of `PadLEDs::image`.
		for (int32_t y = kDisplayHeight; y < deluge::gfx::grid::kMaxGridRows; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(oversized[y][x] == RGB{0, 0, 0}).to_be_true();
			}
		}
	});

	it("answers the opacity query without drawing, which is what the no-image form is for", _ {
		Rig r;
		r.base.occludes = true;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		blank_legacy_pads();

		// `UI`'s contract: quizzable without any rendering (`gui/ui/ui.h:140-142`). Legacy asks it
		// this way in `uiNeedsRendering` (`gui/ui/ui.cpp:292`, `:295`) and in `closeUI`
		// (`:186-187`).
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_true();
		expect(static_cast<::UI&>(r.guest).renderSidebar()).to_be_true();
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(PadLEDs::image[y][x] == RGB{0, 0, 0}).to_be_true();
			}
		}
	});

	it("returns the top scene's opacity, in both directions", _ {
		Rig r;
		expect(r.open()).to_be_true();

		// An overlay scene is transparent over the pads, which is what `SoundEditor` is: it does
		// not override either hook, so it takes `UI`'s `return false`, and `MenuScene` is
		// `is_grid_overlay()` (`scenes/menu/menu_scene.h:473-474`).
		r.base.overlay = true;
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_false();
		expect(static_cast<::UI&>(r.guest).renderSidebar()).to_be_false();

		// A scene that is not an overlay owns the pads.
		r.base.overlay = false;
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_true();
		expect(static_cast<::UI&>(r.guest).renderSidebar()).to_be_true();

		// And it is the TOP scene that answers, not the root: push an overlay over a base that is
		// not one, and the answer flips.
		r.over.overlay = true;
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_false();
	});

	// ---- canSeeViewUnderneath, which is NOT the OLED question --------------------------------

	it("reports the view underneath as visible unless the top scene occludes the grid", _ {
		Rig r;
		expect(r.open()).to_be_true();

		// `MenuScene::occludes_grid()` is false (`scenes/menu/menu_scene.h:475-476`), and
		// `SoundEditor::canSeeViewUnderneath()` is true (`gui/ui/sound_editor.h:121`) -- the guest
		// has to land on legacy's answer, because `UI::graphicsRoutine` runs the root view's
		// animation only while it does (`gui/ui/ui.cpp:47-51`).
		r.base.occludes = false;
		expect(r.guest.canSeeViewUnderneath()).to_be_true();

		r.base.occludes = true;
		expect(r.guest.canSeeViewUnderneath()).to_be_false();

		// It is emphatically not `oled_transparent(Main)`, which this scene never changes: legacy
		// keeps OLED transparency in the `oledShowsUIUnderneath` member instead (`gui/ui/ui.h:158`),
		// and `Slicer` holds the two at opposite values on one object (`gui/ui/slicer.h:33-38`).
		expect(r.guest.oledShowsUIUnderneath).to_be_false();
	});

	// ---- greyout ------------------------------------------------------------------------------

	it("forwards the top scene's greyout mask, and obeys the UI underneath for an empty one", _ {
		Rig r;
		expect(r.open()).to_be_true();

		// `MenuScene::greyout_mask` spells legacy's `doGreyout == false` as an all-zero mask
		// (`scenes/menu/menu_scene.cpp:1697-1729`), and legacy's false means "obey UI under me"
		// (`gui/ui/ui.h:136-138`). The two out-parameters are accumulators the caller zeroed and
		// the next UI down is about to be asked with (`gui/ui/ui.cpp:73-83`), so a false answer must
		// leave them alone.
		r.base.greyout = {};
		uint32_t cols = 0xDEADBEEF;
		uint32_t rows = 0xFEEDFACE;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_false();
		expect(cols).to_equal(uint32_t(0xDEADBEEF));
		expect(rows).to_equal(uint32_t(0xFEEDFACE));

		// A non-zero mask is legacy's `doGreyout == true`, and both halves have to arrive: cols and
		// rows are separate masks that OR into a cross, not a rectangle (`ui/grid/effects.h:33-35`).
		r.base.greyout = {0xFFFFFFFEu, 0x00000005u};
		cols = 0;
		rows = 0;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_true();
		expect(cols).to_equal(uint32_t(0xFFFFFFFE));
		expect(rows).to_equal(uint32_t(0x00000005));

		// Rows alone is still a greyout: a mask with only rows set must not be read as "empty".
		r.base.greyout = {0u, 0x00000080u};
		cols = 0;
		rows = 0;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_true();
		expect(cols).to_equal(uint32_t(0));
		expect(rows).to_equal(uint32_t(0x00000080));

		// And it is the top scene that is asked: an overlay's own mask replaces the base's.
		r.over.greyout = {0x0000000Fu, 0u};
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		cols = 0;
		rows = 0;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_true();
		expect(cols).to_equal(uint32_t(0x0000000F));
		expect(rows).to_equal(uint32_t(0));
	});

	// ---- exitUI, both directions --------------------------------------------------------------

	it("pops one shell scene on the long-press exit and stays open while the stack still has one", _ {
		Rig r;
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(2));

		const ActionResult result = r.guest.exitUI();

		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(r.over.exits).to_equal(1);
		// Consumed, and the guest is still on legacy's hierarchy: closing here would pop it a level
		// early, taking the whole guest down for a Back that only left one scene inside it.
		expect(result == ActionResult::DEALT_WITH).to_be_true();
		expect(fakelegacyui::closed.empty()).to_be_true();
	});

	it("closes itself when the pop empties the stack, because nothing else will", _ {
		Rig r;
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));

		const ActionResult result = r.guest.exitUI();

		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		expect(r.base.exits).to_equal(1);
		// `closeUI(this)`, the same thing `RenameUI::exitUI` and `SampleMarkerEditor::exitUI` do
		// (`gui/ui/rename/rename_ui.cpp:88-91`, `gui/ui/sample_marker_editor.cpp:680-683`) and the
		// same thing `SoundEditor::exitCompletely` does at `gui/ui/sound_editor.cpp:761`. Without
		// it the user is trapped: neither reader of the return value closes anything --
		// `gui/ui_timer_manager.cpp:149-155` tests only `REMIND_ME_OUTSIDE_CARD_ROUTINE`.
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::closed[0] == static_cast<::UI*>(&r.guest)).to_be_true();
		// **`ACTIONED_AND_CAUSED_CHANGE`, changed from `NOT_DEALT_WITH` by F1 task 6.** `ui.h`'s
		// contract for this hook is "returns whether a UI exited" (`gui/ui/ui.h:152-153`), a UI
		// did, and every legacy self-closing implementation answers this value -- including the
		// tenant the guest stands in for, whose `exitUI` is `exitCompletely()`
		// (`gui/ui/sound_editor.h:130`, returning at `gui/ui/sound_editor.cpp:771`).
		expect(result == ActionResult::ACTIONED_AND_CAUSED_CHANGE).to_be_true();
	});

	it("is safe on an already-empty stack: it closes once and pops nothing", _ {
		Rig r;
		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		const ActionResult result = r.guest.exitUI();
		expect(result == ActionResult::ACTIONED_AND_CAUSED_CHANGE).to_be_true();
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
	});

	// ---- the SHORT Back, which is the exit a user actually performs (F1 task 11) ---------------
	//
	// `exitUI` above is the LONG press only: `Buttons` arms `TimerName::BACK_MENU_EXIT` on Back
	// down (`hid/buttons.cpp:114-121`) and the manager calls `exitUI` from that arm alone
	// (`gui/ui_timer_manager.cpp:149-155`). A short Back is fed into the shell like any other
	// button and routed inside `Shell::tick`, where a scene can take itself off the stack --
	// `MenuScene`'s `Action::Kind::Remove` at depth 0. Before F1 task 11 nothing closed the guest
	// when that emptied the stack, and the level-3 A/B measured what it left: `shellHostUI` still
	// current, drawing nothing, swallowing every later press.

	it("closes itself when a scene removes itself during the tick and empties the stack", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.remove_self_on_event = true;
		fakelegacyui::reset();

		// The button is fed, not actioned: routing happens in `Shell::tick`, which only
		// `timerCallback` calls. So nothing has closed yet at this point...
		(void)r.guest.buttonAction(deluge::hid::button::BACK, true, false);
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(fakelegacyui::closed.empty()).to_be_true();

		// Reset AFTER the press and before the tick, so the arm assertion below is about the tick
		// alone: `buttonAction` arms `kMinRearmMs` on its way out (`armTickSoon`), which is correct
		// and would otherwise be the thing the last expectation saw.
		fakelegacyui::reset();

		// ...and the tick is what empties the stack and must therefore be what closes.
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::closed[0] == static_cast<::UI*>(&r.guest)).to_be_true();
		// And no re-arm on the close path: `closeUI` unsets `TimerName::UI_SPECIFIC` itself
		// (`gui/ui/ui.cpp:197`) before handing focus to the UI beneath, so a timer armed here would
		// either be undone one line later or deliver this guest's `timerCallback` to that UI
		// (`gui/ui_timer_manager.cpp:143`).
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(-1));
	});

	it("does NOT close on a tick that leaves the stack occupied, and re-arms as usual", _ {
		// The other direction, and the one that makes the example above mean something: a guest
		// that closed on every tick would pass that one too.
		Rig r;
		r.base.timer = 120;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();

		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(fakelegacyui::closed.empty()).to_be_true();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(120));
	});

	// ---- MIDI learn: the two `UI` virtuals, F1 task 10 ----------------------------------------
	//
	// These examples are about **delivery**, which is all this suite can be about: the far side of
	// both hooks is `gui/ui/shell_guest_services.h`'s two free functions, faked here for the reason
	// `wireShellGuestServices` is (`tests/spec/CMakeLists.txt`: the `deluge_scenes` closure is
	// deliberately not built). So what is driven is the guest's own composition -- menu first, then
	// the shell's chain, and what each hook returns to legacy's `if`/`else` -- and NOT
	// `MenuScene::learn_note_on`, whose behaviour is `sim/scenes_spec`'s.
	//
	// The mode/shift gate on the program-change hook is likewise on the far side (it reads
	// `currentUIMode` and `Buttons`, neither of which this suite links), and is covered where the
	// real closure exists.

	it("the note-on hook offers the message to the menu first and routes it into the shell when the "
	   "menu declines", _ {
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		fakelegacyui::menu_learn_claims = false;

		const bool handled = r.guest.noteOnReceivedForMidiLearn(fake_cable(), 9, 64, 100);

		// Offered to the menu, with the dispatcher's own three arguments and no fourth: legacy's
		// `SoundEditor::noteOnReceivedForMidiLearn` drops the velocity too
		// (`gui/ui/sound_editor.cpp:1613`).
		expect(fakelegacyui::menu_learn_note_ons).to_equal(1);
		expect(fakelegacyui::last_menu_learn.cable == &fake_cable()).to_be_true();
		expect(fakelegacyui::last_menu_learn.channel).to_equal(int32_t(9));
		expect(fakelegacyui::last_menu_learn.data).to_equal(int32_t(64));

		// …and then routed, synchronously. `Shell::route_midi` does NOT queue, so the scene has the
		// event before this line -- which is the whole reason that entry point exists (a queued feed
		// could not produce the bool legacy's `else` is decided by).
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].ev.kind == UiEvent::Kind::MidiNoteOn).to_be_true();
		expect(r.base.seen[0].ev.channel).to_equal(uint8_t(9));
		expect(r.base.seen[0].ev.data1).to_equal(uint8_t(64));
		expect(r.base.seen[0].ev.data2).to_equal(uint8_t(100));
		expect(r.base.seen[0].ev.cable == &fake_cable()).to_be_true();

		// `PatternScene` declines and this rig installs no services (`Services{}` -- the real three
		// arrive through `wireShellGuestServices`, which is a counter here), so nothing handled it
		// and the guest says so. Legacy then runs `view.noteOnReceivedForMidiLearn`
		// (`playback/playback_handler.cpp:2970`), which is the correct fallback for a guest whose
		// chain claimed nothing.
		expect(handled).to_be_false();
	});

	it("the note-on hook stops at the menu when the menu claims, and routes nothing", _ {
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		fakelegacyui::menu_learn_claims = true;

		const bool handled = r.guest.noteOnReceivedForMidiLearn(fake_cable(), 9, 64, 100);

		expect(handled).to_be_true();
		expect(fakelegacyui::menu_learn_note_ons).to_equal(1);
		// **Nothing routed.** This is the half that regresses silently: a hook that always fed the
		// shell as well would deliver one message twice, once to the focused menu item and once to
		// `services::MidiLearn`. Legacy's `SoundEditor` returning true is what stops `view` seeing
		// it, and `MidiLearn` is `View`'s port.
		expect(r.base.seen.size()).to_equal(size_t(0));
	});

	it("the program-change hook forwards to the menu and returns its answer, and never routes", _ {
		// Legacy's `View::pcReceivedForMIDILearn` is an EMPTY BODY (`gui/views/view.h:63`), so there
		// is nothing for `services::MidiLearn` to port, no `UiEvent` kind for program change and
		// nothing for this hook to route -- unlike its note-on neighbour, which has both halves.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		fakelegacyui::menu_learn_claims = true;

		expect(r.guest.pcReceivedForMidiLearn(fake_cable(), 4, 12)).to_be_true();
		expect(fakelegacyui::menu_learn_program_changes).to_equal(1);
		expect(fakelegacyui::last_menu_learn.cable == &fake_cable()).to_be_true();
		expect(fakelegacyui::last_menu_learn.channel).to_equal(int32_t(4));
		expect(fakelegacyui::last_menu_learn.data).to_equal(int32_t(12));
		expect(r.base.seen.size()).to_equal(size_t(0));
		expect(fakelegacyui::menu_learn_note_ons).to_equal(0);

		// And the other direction, so "returns its answer" is a claim about the value rather than
		// about the one value that happened to be set.
		fakelegacyui::reset();
		fakelegacyui::menu_learn_claims = false;
		expect(r.guest.pcReceivedForMidiLearn(fake_cable(), 4, 12)).to_be_false();
		expect(fakelegacyui::menu_learn_program_changes).to_equal(1);
		expect(r.base.seen.size()).to_equal(size_t(0));
	});

	// ---- the hooks deliberately left to the base ----------------------------------------------

	it("does not override graphicsRoutine, so the base's forward to the root view survives", _ {
		Rig r;
		r.base.occludes = false;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();

		static_cast<::UI&>(r.guest).graphicsRoutine();

		// **What this proves, precisely: non-override.** The fake replaces `UI::graphicsRoutine`'s
		// body with a counter (`gui/support/fake_legacy_ui.cpp`), because the real body needs
		// `getRootUI()` and a legacy view tree this suite does not build -- so reaching the counter
		// says the call landed on the base, and says nothing about a root view animating. That the
		// base's real body then runs the root's animation while `canSeeViewUnderneath()` is a claim
		// about `gui/ui/ui.cpp:47-51`, read, not driven here. The example's job is the half that can
		// regress silently: `ShellHostUI` adding an override.
		expect(fakelegacyui::graphics_routines).to_equal(1);
		// And the predicate the base gates on, evaluated by the fake, is the one the guest answers
		// true for on a non-occluding scene -- so if a future override did appear, the thing it
		// would be suppressing is a live `true`.
		expect(r.guest.canSeeViewUnderneath()).to_be_true();
	});

	it("asks the shell for a repaint when the display or language changed, and still tells legacy", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		// A tick with nothing new leaves the gates shut, so nothing is blitted and legacy's dirty
		// flag stays down.
		(void)fakelegacy::consume_oled_frame();
		r.guest.shell().tick(1);
		expect(fakelegacy::oled_marked()).to_be_false();
		fakelegacyui::reset();

		r.guest.displayOrLanguageChanged();
		// Legacy still hears about it: the base's `renderUIsForOled()` is what sets
		// `doesOLEDNeedRendering` (`gui/ui/ui.cpp:53-55`).
		expect(fakelegacyui::oled_redraws).to_equal(1);
		// And the shell will actually re-render on its next tick, rather than serving the frame it
		// happens to be holding.
		r.guest.shell().tick(2);
		expect(fakelegacy::oled_marked()).to_be_true();
	});
	// ---- input: pads ---------------------------------------------------------------------------

	it("routes a pad to the top scene with its coordinates and velocity intact, on the next tick", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		fakelegacyui::system_time_s = 0.25; // nowMs() == 250
		const ActionResult result = r.guest.padAction(5, 3, 137);

		// **Nothing is routed by the hook itself.** `Shell::on_board_event` feeds the lowering;
		// `Shell::pump_input` is what routes, and only `tick` calls it (`shell/shell.cpp:579-589`).
		expect(r.base.seen.empty()).to_be_true();

		r.guest.shell().tick(250);
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].ev.kind == UiEvent::Kind::Pad).to_be_true();
		expect(static_cast<int>(r.base.seen[0].ev.x)).to_equal(5);
		expect(static_cast<int>(r.base.seen[0].ev.y)).to_equal(3);
		expect(static_cast<int>(r.base.seen[0].ev.velocity)).to_equal(137);
		expect(r.base.seen[0].ev.on).to_be_true();
		// `MatrixDriver::padAction` collapses every value but `REMIND_ME_OUTSIDE_CARD_ROUTINE` to
		// `DEALT_WITH` before its caller sees it (`hid/matrix/matrix_driver.cpp:79-83`), and the
		// guest never produces that one -- it defers instead.
		expect(result == ActionResult::DEALT_WITH).to_be_true();
	});

	it("carries a pad release through as a release rather than a second press", _ {
		Rig r;
		expect(r.open()).to_be_true();
		(void)r.guest.padAction(5, 3, 255);
		r.guest.shell().tick(1);
		r.base.seen.clear();

		(void)r.guest.padAction(5, 3, 0);
		r.guest.shell().tick(2);

		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].ev.on).to_be_false();
		expect(static_cast<int>(r.base.seen[0].ev.velocity)).to_equal(0);
	});

	it("drops a pad coordinate that would fold onto a real one when narrowed", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		// 256 is 0 in a `uint8_t`, and `InputLowering::on_pad`'s own bounds check runs on the
		// narrowed value -- so without the guard in `padAction` this arrives as a press on column
		// 0, a pad nobody touched. `padAction` is a public virtual legacy UIs call on each other
		// (`gui/ui/sound_editor.cpp:1474-1477`), so its domain is not `MatrixDriver`'s alone.
		(void)r.guest.padAction(256, 0, 255);
		(void)r.guest.padAction(-1, 0, 255);
		r.guest.shell().tick(1);
		expect(r.base.seen.empty()).to_be_true();

		// Vary only the coordinate: column 0 itself still gets through, so the refusal above is
		// about the fold and not about the guest refusing pads generally.
		(void)r.guest.padAction(0, 0, 255);
		r.guest.shell().tick(2);
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(static_cast<int>(r.base.seen[0].ev.x)).to_equal(0);
	});

	// ---- input: the seam with `Scene::grid_pad_claim()` ----------------------------------------

	it("feeds a pad the scene claims, and does not forward it", _ {
		// `GridPadClaim::All` is the default, so this is every scene that existed before this seam.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::All;

		(void)r.guest.padAction(3, 4, 64);
		r.guest.shell().tick(1);

		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(fakelegacyui::pad_forwards).to_equal(0);
	});

	it("forwards a pad the scene does not claim, and does not feed it", _ {
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::None;

		(void)r.guest.padAction(3, 4, 64);
		r.guest.shell().tick(1);

		expect(r.base.seen.empty()).to_be_true();
		expect(fakelegacyui::pad_forwards).to_equal(1);
		expect(fakelegacyui::last_pad_forward.x).to_equal(int32_t(3));
		expect(fakelegacyui::last_pad_forward.y).to_equal(int32_t(4));
		expect(fakelegacyui::last_pad_forward.velocity).to_equal(int32_t(64));
	});

	it("under ShortcutsOnly feeds a shift-held main-grid press and forwards everything else", _ {
		// Spec §3.1: `potentialShortcutPadAction`'s gate is `on && x < kDisplayWidth && shift`
		// (`gui/ui/sound_editor.cpp:1169-1186`), and past that gate it claims unconditionally
		// (`:1431`).
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::ShortcutsOnly;

		fakelegacyui::shift_button_pressed = true;
		(void)r.guest.padAction(3, 4, 64);
		r.guest.shell().tick(1);
		expect(r.base.seen.size()).to_equal(size_t(1));

		(void)r.guest.padAction(kDisplayWidth + 1, 4, 64); // sidebar: never a shortcut
		(void)r.guest.padAction(3, 4, 0);                  // release: never a shortcut
		fakelegacyui::shift_button_pressed = false;
		(void)r.guest.padAction(3, 4, 64); // no shift
		expect(fakelegacyui::pad_forwards).to_equal(3);
	});

	it("asks the input top, not the top", _ {
		// A popup is `input_transparent()`, so it is not the scene whose grid this is. Asking
		// `top()` would let a popup's default `All` swallow a pad the menu beneath would forward.
		Rig r;
		r.base.claim = GridPadClaim::None;
		expect(r.open()).to_be_true();
		r.over.passive = true; // a popup / console line: drawn over, never taking input
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		fakelegacyui::reset();

		(void)r.guest.padAction(3, 4, 64);

		expect(fakelegacyui::pad_forwards).to_equal(1);
	});

	it("still arms the tick after a forward", _ {
		// Load-bearing, not housekeeping: the `Exit` arm empties the guest's shell stack, and
		// `ShellHostUI::timerCallback` is what then closes the guest (`shell_host_ui.cpp:553-556`).
		// With no tick armed, the guest would sit open over an empty stack.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::None;

		(void)r.guest.padAction(3, 4, 64);

		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));
	});

	// ---- input: the card routine ---------------------------------------------------------------

	it("defers a press that arrives during the card routine, and replays it in order when it ends", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		// Legacy's answer here is `REMIND_ME_OUTSIDE_CARD_ROUTINE`
		// (`gui/ui/sound_editor.cpp:1460-1463`), which `dispatchInputEvent` turns into one stashed
		// event replayed after the routine (`deluge.cpp:247-250`, `:295-305`). The port is
		// `InputLowering::set_sd_busy`, so the press must be **deferred, not dropped**.
		fakelegacyui::sd_routine_active = true;
		(void)r.guest.padAction(4, 2, 255);
		r.guest.shell().tick(10);
		expect(r.base.seen.empty()).to_be_true();

		// Vary **only** the flag: the same shape of press, on the same open guest, with the routine
		// over. If this second press were the thing that made it work, the first would still be
		// missing below.
		fakelegacyui::sd_routine_active = false;
		(void)r.guest.padAction(6, 1, 255);
		r.guest.shell().tick(20);

		expect(r.base.seen.size()).to_equal(size_t(2));
		// Arrival order, the deferred one first (`shell/input_lowering.cpp:142-160`).
		expect(static_cast<int>(r.base.seen[0].ev.x)).to_equal(4);
		expect(static_cast<int>(r.base.seen[0].ev.y)).to_equal(2);
		expect(static_cast<int>(r.base.seen[1].ev.x)).to_equal(6);
		expect(static_cast<int>(r.base.seen[1].ev.y)).to_equal(1);
	});

	it("defers a button press during the card routine too, from the parameter legacy passed", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		// `buttonAction` takes `inCardRoutine` rather than reading the scheduler, because that is
		// the caller's own answer (`deluge.cpp:256`). So the *parameter* is what must gate this,
		// and `sd_routine_active` is left false to prove the parameter is being read.
		expect(fakelegacyui::sd_routine_active).to_be_false();
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true, true);
		r.guest.shell().tick(10);
		expect(r.base.seen.empty()).to_be_true();

		(void)r.guest.buttonAction(deluge::hid::button::LOAD, false, false);
		r.guest.shell().tick(20);
		expect(r.base.seen.size()).to_equal(size_t(2));
		expect(r.base.seen[0].ev.button == ButtonId::Load).to_be_true();
		expect(r.base.seen[0].ev.on).to_be_true();
		expect(r.base.seen[1].ev.on).to_be_false();
	});

	// ---- input: buttons ------------------------------------------------------------------------

	it("decodes a legacy Button back into matrix coordinates, one id per button", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		// LOAD is matrix (6, 1) and SAVE is (6, 3): the same column, so a decode that dropped the
		// row would give both the same id.
		const ActionResult result = r.guest.buttonAction(deluge::hid::button::LOAD, true, false);
		(void)r.guest.buttonAction(deluge::hid::button::SAVE, true, false);
		r.guest.shell().tick(1);

		expect(r.base.seen.size()).to_equal(size_t(2));
		expect(r.base.seen[0].ev.kind == UiEvent::Kind::Button).to_be_true();
		expect(r.base.seen[0].ev.button == ButtonId::Load).to_be_true();
		expect(r.base.seen[1].ev.button == ButtonId::Save).to_be_true();

		// **`NOT_DEALT_WITH`, and it is what keeps `Buttons::buttonAction`'s tail running** --
		// `commandToggleShift` (`hid/buttons.cpp:169-171`), the transport, and the mod-encoder
		// button dispatch at `:226-231`. `DEALT_WITH` would `goto dealtWith` at `:139-141`.
		expect(result == ActionResult::NOT_DEALT_WITH).to_be_true();
	});

	it("delivers an encoder button as an EncoderButton, not a Button", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		(void)r.guest.buttonAction(deluge::hid::button::SELECT_ENC, true, false);
		r.guest.shell().tick(1);

		// `InputLowering::on_button` splits on `is_encoder_button` (`shell/input_lowering.cpp:300-303`),
		// which is a fact about the id the matrix decode produced -- so this also proves the decode
		// landed on the right cell rather than merely on *a* cell.
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].ev.kind == UiEvent::Kind::EncoderButton).to_be_true();
		expect(r.base.seen[0].ev.button == ButtonId::SelectEnc).to_be_true();
	});

	it("seeds the shell's sticky-shift setting from runtimeFeatureSettings when it opens", _ {
		// Legacy reads `ShiftIsSticky` at shift-release time (`hid/buttons.cpp:250`); the shell reads
		// its own `LoweringConfig` field, which defaults to false. Unseeded, a sticky-shift user's
		// latched shift is invisible to `ctx.shift` -- the whole gate on the menu's shortcut-pad
		// jump (`scenes/menu/menu_scene.cpp:1631`).
		Rig on;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
		expect(on.open()).to_be_true();
		expect(on.guest.shell().lowering().shift_is_sticky()).to_be_true();

		// Vary only the setting, on an otherwise identical guest.
		Rig off;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
		expect(off.open()).to_be_true();
		expect(off.guest.shell().lowering().shift_is_sticky()).to_be_false();
	});

	it("latches shift on a tap once the setting is seeded, and does not without it", _ {
		// The seed is only worth having if it changes what a scene sees, so this drives the whole
		// gesture: a tap shorter than `hold_time_ms` (100), then a pad, and asks whether `ctx.shift`
		// survived the release.
		Rig sticky;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
		expect(sticky.open()).to_be_true();
		fakelegacyui::system_time_s = 1.000;
		(void)sticky.guest.buttonAction(deluge::hid::button::SHIFT, true, false);
		fakelegacyui::system_time_s = 1.010; // a 10 ms tap
		(void)sticky.guest.buttonAction(deluge::hid::button::SHIFT, false, false);
		sticky.guest.shell().tick(10);
		sticky.base.seen.clear();
		(void)sticky.guest.padAction(3, 3, 255);
		sticky.guest.shell().tick(20);
		const PatternScene::Seen* latched = first_of(sticky.base, UiEvent::Kind::Pad);
		expect(latched != nullptr).to_be_true();
		expect(latched->shift).to_be_true();
		expect(latched->sticky).to_be_true();

		// Vary only the setting; the identical gesture must not latch.
		Rig plain;
		// The `Rig` constructor has already put the feature back to Off; naming it is what makes the
		// contrast with the block above explicit rather than incidental.
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
		expect(plain.open()).to_be_true();
		fakelegacyui::system_time_s = 1.000;
		(void)plain.guest.buttonAction(deluge::hid::button::SHIFT, true, false);
		fakelegacyui::system_time_s = 1.010;
		(void)plain.guest.buttonAction(deluge::hid::button::SHIFT, false, false);
		plain.guest.shell().tick(10);
		plain.base.seen.clear();
		(void)plain.guest.padAction(3, 3, 255);
		plain.guest.shell().tick(20);
		const PatternScene::Seen* unlatched = first_of(plain.base, UiEvent::Kind::Pad);
		expect(unlatched != nullptr).to_be_true();
		expect(unlatched->shift).to_be_false();
	});

	it("re-reads hold_time_ms from FlashStorage::holdTime on every event, out of audio samples", _ {
		// The other half of the sticky-shift divergence, and the one F1 task 6 could not close: the
		// threshold a tap is measured against is `FlashStorage::holdTime` in legacy
		// (`hid/buttons.cpp:264`) and a fixed 100 ms in the shell. `holdTime` is in **samples**, so
		// this is a unit conversion as well as a copy -- a seed that forwarded the number unchanged
		// would give 4410 ms for legacy's default.
		//
		// Asserted after a *feed*, not after `open()`: the read is in `feed()`, which is where
		// legacy's own read is. See the next example for why that placement is load-bearing.
		const auto seeded_by_a_press = [](int32_t hold_samples) {
			FlashStorage::holdTime = hold_samples;
			Rig r;
			(void)r.open();
			(void)r.guest.padAction(1, 1, 255);
			return r.guest.shell().lowering().hold_time_ms();
		};
		expect(seeded_by_a_press((2 * kSampleRate) / 20)).to_equal(uint16_t(100));  // legacy's default
		expect(seeded_by_a_press((20 * kSampleRate) / 20)).to_equal(uint16_t(1000)); // the item's max
		expect(seeded_by_a_press((1 * kSampleRate) / 20)).to_equal(uint16_t(50));    // and its min
		FlashStorage::holdTime = (2 * kSampleRate) / 20;
	});

	it("follows a change made while the guest is open, which is the only way the ported item can", _ {
		// **The placement is the point.** The first version of this seeded once, in `opened()`, on
		// the reasoning that a guest that is not open cannot be observing the threshold. That is
		// backwards for the one writer reachable *only while the guest is open*:
		// `nodes::set_default_hold_time` (`scenes/menu/nodes/settings.cpp`) is the ported settings
		// item and writes `FlashStorage::holdTime` from inside the shell, so a per-open seed would
		// lag it by a whole guest session -- exactly the hole this task recorded for the screensaver
		// settings and, in its first version, reproduced here.
		//
		// The guest is opened ONCE and the flash global is moved underneath it, which is what the
		// ported item does; only then is the gesture driven. An `opened()`-only seed fails here and
		// passes the example above, which is why both exist.
		Rig r;
		FlashStorage::holdTime = (2 * kSampleRate) / 20; // 100 ms at open
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
		expect(r.open()).to_be_true();

		FlashStorage::holdTime = (10 * kSampleRate) / 20; // the ported item writes 500 ms
		fakelegacyui::system_time_s = 1.000;
		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, true, false);
		fakelegacyui::system_time_s = 1.200; // a 200 ms tap: a hold at 100 ms, a tap at 500
		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, false, false);
		expect(r.guest.shell().lowering().hold_time_ms()).to_equal(uint16_t(500));
		r.guest.shell().tick(200);
		r.base.seen.clear();
		(void)r.guest.padAction(3, 3, 255);
		r.guest.shell().tick(210);
		const PatternScene::Seen* pad = first_of(r.base, UiEvent::Kind::Pad);
		expect(pad != nullptr).to_be_true();
		expect(pad->sticky).to_be_true();

		FlashStorage::holdTime = (2 * kSampleRate) / 20;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
	});

	it("a tap that legacy would latch latches here too, once the threshold is seeded", _ {
		// The seed made visible where it matters: the same 200 ms shift tap, with only
		// `FlashStorage::holdTime` varied, must reach a scene as a latch on one setting and not on
		// the other. With the shell's fixed 100 ms this gesture could never latch at all, which is
		// what the divergence cost a user who had raised the setting.
		const auto latches_with = [](int32_t hold_samples) {
			FlashStorage::holdTime = hold_samples;
			Rig r;
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
			(void)r.open();
			fakelegacyui::system_time_s = 1.000;
			(void)r.guest.buttonAction(deluge::hid::button::SHIFT, true, false);
			fakelegacyui::system_time_s = 1.200; // a 200 ms tap, the same every time
			(void)r.guest.buttonAction(deluge::hid::button::SHIFT, false, false);
			r.guest.shell().tick(200);
			r.base.seen.clear();
			(void)r.guest.padAction(3, 3, 255);
			r.guest.shell().tick(210);
			const PatternScene::Seen* pad = first_of(r.base, UiEvent::Kind::Pad);
			return pad != nullptr && pad->sticky;
		};
		expect(latches_with((2 * kSampleRate) / 20)).to_be_false(); // 100 ms: 200 is a hold
		expect(latches_with((10 * kSampleRate) / 20)).to_be_true(); // 500 ms: 200 is a tap
		expect(latches_with((20 * kSampleRate) / 20)).to_be_true(); // 1 s
		expect(latches_with((1 * kSampleRate) / 20)).to_be_false(); // 50 ms
		FlashStorage::holdTime = (2 * kSampleRate) / 20;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
	});

	it("installs the three services on every open, not only the first", _ {
		// `wireShellGuestServices()` is what `Shell::set_services` is finally called from
		// (migration §10.2 item 7's boot half). This suite never builds the `deluge_scenes` closure,
		// so what it can assert is that `opened()` calls it -- **again on every open**, because the
		// song and `View`'s mod target can both have moved since the last one, and an unseeded
		// `ModKnobs` blanks the mod LEDs and both knob indicator rings rather than doing nothing.
		// What the three installed objects actually are is asserted where they can be built:
		// `sim/scenes_spec/boot_wiring_spec.cpp`.
		Rig r;
		expect(fakelegacyui::service_wirings).to_equal(0);
		expect(r.open()).to_be_true();
		expect(fakelegacyui::service_wirings).to_equal(1);
		(void)r.guest.exitUI();
		expect(r.open()).to_be_true();
		expect(fakelegacyui::service_wirings).to_equal(2);

		// A refused open does not wire: `openUI` rolls the hierarchy back on `false`
		// (`gui/ui/ui.cpp:237-242`), so there is no guest to serve.
		Rig refused;
		expect(fakelegacyui::service_wirings).to_equal(0); // the Rig ctor reset the counter
		refused.guest.setInitialScene(SceneId{SceneKind::Null, 0});
		expect(refused.guest.opened()).to_be_false();
		expect(fakelegacyui::service_wirings).to_equal(0);
	});

	it("hands the knob indicator rings back on the close path, and only on the close path", _ {
		// `GuestSurfaceSet::set_indicator` writes the hardware without legacy's
		// `knobIndicatorLevels[]` shadow, so `actuallySetKnobIndicatorLevel`'s early-out
		// (`hid/led/indicator_leds.cpp:186-191`) would otherwise suppress the write that puts the
		// rings right when the guest goes away. F1 task 7 named this fix and named task 8's
		// installation of `ModKnobs` -- the one `frame.indicator(...)` declarer in `src/scenes` --
		// as the condition that makes it live.
		Rig r;
		expect(r.open(kBase)).to_be_true();
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();

		// The survive path: a UI did not exit, so nothing is handed back.
		expect(r.guest.exitUI() == ActionResult::DEALT_WITH).to_be_true();
		expect(fakelegacyui::knob_indicator_clears).to_equal(0);
		expect(fakelegacyui::closed.empty()).to_be_true();

		// The close path: cleared, exactly once, and **before** `close()` -- `closeUI` hands focus
		// to the UI beneath on the same call (`gui/ui/ui.cpp:199`) and that is what re-fills the
		// rings, so a clear after it would blank rings nothing then refreshes.
		expect(r.guest.exitUI() == ActionResult::ACTIONED_AND_CAUSED_CHANGE).to_be_true();
		expect(fakelegacyui::knob_indicator_clears).to_equal(1);
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::knob_clear_saw_close).to_be_false();
	});

	it("does not feed the two gold-knob pushes, which legacy delivers twice", _ {
		// `Buttons::buttonAction` calls this hook for MOD_ENCODER_0/1 at `hid/buttons.cpp:134` and
		// then calls `modEncoderButtonAction` for the same press in its tail (`:226-231`), whose
		// base forwards to `view` -- the very function `ModKnobs::mod_encoder_button_action` ports.
		// Feeding the press as well would perform the gesture twice once task 8's `ModKnobs` has a
		// target. Varying the button rather than the guest: SAVE (a button no arm in that tail
		// names) goes through on the same rig, so this is about these two ids and not about
		// `buttonAction` having stopped feeding.
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		// Clear what `opened()` armed, so the arm asserted below can only be this hook's. Legacy
		// does the same thing to the guest all the time -- `openUI`/`closeUI` unset `UI_SPECIFIC`
		// before handing control on (`gui/ui/ui.cpp:197`, `:233`).
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		fakelegacyui::timer_arms.clear();

		// `NOT_DEALT_WITH` is load-bearing on this arm too, and for a reason the drop makes sharper:
		// `DEALT_WITH` would `goto dealtWith` (`hid/buttons.cpp:139-141`) and skip the tail at
		// `:226-231`, so `view` would never see the push either and the gesture would be lost
		// outright rather than handed to legacy.
		expect(r.guest.buttonAction(deluge::hid::button::MOD_ENCODER_0, true, false)
		       == ActionResult::NOT_DEALT_WITH)
		    .to_be_true();
		expect(r.guest.buttonAction(deluge::hid::button::MOD_ENCODER_1, true, false)
		       == ActionResult::NOT_DEALT_WITH)
		    .to_be_true();
		r.guest.shell().tick(10);
		expect(r.base.seen.empty()).to_be_true();

		// **A dropped press is still a re-arm**, and this is asserted before anything else arms the
		// timer: `set_sd_busy(inCardRoutine)` ran above it and may have released a lock, which
		// replays the deferred queue into the ordinary one -- so the guest must still ask for a tick
		// even though it fed nothing itself.
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));

		(void)r.guest.buttonAction(deluge::hid::button::SAVE, true, false);
		r.guest.shell().tick(20);
		expect(r.base.seen.size()).to_equal(size_t(1));
	});

	it("reads shift out of the events it feeds, in both directions", _ {
		Rig r;
		expect(r.open()).to_be_true();

		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, true, false);
		r.guest.shell().tick(1);
		r.base.seen.clear();
		(void)r.guest.padAction(2, 2, 255);
		r.guest.shell().tick(2);

		const PatternScene::Seen* withShift = first_of(r.base, UiEvent::Kind::Pad);
		expect(withShift != nullptr).to_be_true();
		expect(withShift->shift).to_be_true();
		// Not the sticky latch: `LoweringConfig::shift_is_sticky` is false here, which is the
		// divergence from legacy recorded on the class (`@par Shift`, point 1).
		expect(withShift->sticky).to_be_false();

		// Vary only the shift state, on the same pad.
		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, false, false);
		r.guest.shell().tick(3);
		r.base.seen.clear();
		(void)r.guest.padAction(2, 2, 255);
		r.guest.shell().tick(4);

		const PatternScene::Seen* withoutShift = first_of(r.base, UiEvent::Kind::Pad);
		expect(withoutShift != nullptr).to_be_true();
		expect(withoutShift->shift).to_be_false();
	});

	// ---- input: encoders -----------------------------------------------------------------------

	it("multiplies detents back up into edges, so a turn arrives at the size legacy handed over", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		// Legacy hands a `UI` whole **detents** (`hid/encoder_input.cpp:112`, `:124`); the shell's
		// entry point takes **edges** and divides by `edges_per_detent` itself
		// (`shell/input_lowering.cpp:178-186`). Feeding the detent count straight through would
		// halve this to 1.
		const ActionResult result = r.guest.horizontalEncoderAction(3);
		r.guest.shell().tick(1);

		const PatternScene::Seen* turn = first_of(r.base, UiEvent::Kind::Encoder);
		expect(turn != nullptr).to_be_true();
		expect(turn->ev.encoder == deluge::ui::EncoderRole::Horizontal).to_be_true();
		expect(static_cast<int>(turn->ev.delta)).to_equal(3);
		// `interpretEncoders` tests this for `REMIND_ME_OUTSIDE_CARD_ROUTINE` alone
		// (`hid/encoder_input.cpp:129-132`); the deferral replaces it.
		expect(result == ActionResult::DEALT_WITH).to_be_true();
	});

	it("keeps the three function encoders on their own roles", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		(void)r.guest.verticalEncoderAction(-2, false);
		r.guest.shell().tick(1);
		const PatternScene::Seen* vertical = first_of(r.base, UiEvent::Kind::Encoder);
		expect(vertical != nullptr).to_be_true();
		expect(vertical->ev.encoder == deluge::ui::EncoderRole::Vertical).to_be_true();
		expect(static_cast<int>(vertical->ev.delta)).to_equal(-2);

		r.base.seen.clear();
		r.guest.selectEncoderAction(4);
		r.guest.shell().tick(2);
		const PatternScene::Seen* select = first_of(r.base, UiEvent::Kind::Encoder);
		expect(select != nullptr).to_be_true();
		expect(select->ev.encoder == deluge::ui::EncoderRole::Select).to_be_true();
		expect(static_cast<int>(select->ev.delta)).to_equal(4);
	});

	it("saturates a turn past the int8_t edge ceiling instead of wrapping it negative", _ {
		Rig r;
		expect(r.open()).to_be_true();

		// The ceiling, with the arithmetic: `Shell::on_encoder_edges` takes an `int8_t`
		// (`shell/shell.h:294`) and the Deluge is two edges per detent, so 63 detents (126 edges)
		// is the largest that fits and 64 (128 edges) narrows to -128, i.e. **-64 detents**.
		// The guest can be handed more than 63: legacy's `detentDelta` accumulates in
		// `DetentedEncoder::pos` across every pass `interpretEncoders` skips during the SD routine
		// (`hid/encoder_input.cpp:96-106`) or a stem export (`:66-68`).
		r.base.seen.clear();
		(void)r.guest.verticalEncoderAction(63, false);
		r.guest.shell().tick(1);
		const PatternScene::Seen* atCeiling = first_of(r.base, UiEvent::Kind::Encoder);
		expect(atCeiling != nullptr).to_be_true();
		expect(static_cast<int>(atCeiling->ev.delta)).to_equal(63);

		r.base.seen.clear();
		(void)r.guest.verticalEncoderAction(64, false);
		r.guest.shell().tick(2);
		const PatternScene::Seen* past = first_of(r.base, UiEvent::Kind::Encoder);
		expect(past != nullptr).to_be_true();
		// Not -64, and not 0 either: saturated, the choice legacy makes for its own two narrowing
		// encoders (`hid/encoder_input.cpp:117`).
		expect(static_cast<int>(past->ev.delta)).to_equal(63);

		r.base.seen.clear();
		(void)r.guest.verticalEncoderAction(-500, false);
		r.guest.shell().tick(3);
		const PatternScene::Seen* negative = first_of(r.base, UiEvent::Kind::Encoder);
		expect(negative != nullptr).to_be_true();
		expect(static_cast<int>(negative->ev.delta)).to_equal(-63);
	});

	it("resolves an encoder role through the profile's table, not a fixed board index", _ {
		// Only the encoder order differs from the Deluge's, so the panel, the grid and every other
		// answer are the ones the examples above used.
		Rig r{kDelugeBoard, profile_permuted_encoders()};
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		(void)r.guest.horizontalEncoderAction(2);
		r.guest.shell().tick(1);

		// Index 1 on this profile is Tempo, and index 2 is Horizontal.
		const PatternScene::Seen* turn = first_of(r.base, UiEvent::Kind::Encoder);
		expect(turn != nullptr).to_be_true();
		expect(turn->ev.encoder == deluge::ui::EncoderRole::Horizontal).to_be_true();
		expect(static_cast<int>(turn->ev.delta)).to_equal(2);
	});

	it("drops a turn for a role the board has no encoder for, and only that role", _ {
		Rig r{kDelugeBoard, profile_no_horizontal()};
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		(void)r.guest.horizontalEncoderAction(5);
		r.guest.shell().tick(1);
		expect(first_of(r.base, UiEvent::Kind::Encoder) == nullptr).to_be_true();

		// Vary the role, not the board: vertical is in this table and still arrives.
		(void)r.guest.verticalEncoderAction(5, false);
		r.guest.shell().tick(2);
		const PatternScene::Seen* vertical = first_of(r.base, UiEvent::Kind::Encoder);
		expect(vertical != nullptr).to_be_true();
		expect(vertical->ev.encoder == deluge::ui::EncoderRole::Vertical).to_be_true();
	});

	it("feeds nothing for a zero turn", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		(void)r.guest.horizontalEncoderAction(0);
		r.guest.shell().tick(1);
		expect(r.base.seen.empty()).to_be_true();
		expect(r.guest.shell().lowering().had_input_this_tick()).to_be_false();
	});

	it("stamps events with the clock nowMs() reads, not the tick's", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		fakelegacyui::system_time_s = 1.5; // nowMs() == 1500
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true, false);
		// `end_tick` re-stamps the snapshot with the tick's own clock, which is what a scene reads
		// as `ctx.now_ms` (`shell/shell.cpp:567-577`, `:579-589`). The **feed's** clock is what the
		// press keeps, and that one comes from `nowMs()` -- so the two numbers being different is
		// what proves the hook stamped its own rather than borrowing the tick's.
		r.guest.shell().tick(1700);

		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].now_ms).to_equal(uint32_t(1700));
		expect(r.guest.shell().lowering().held_since(ButtonId::Load)).to_equal(uint32_t(1500));
	});

	it("reads nowMs() from getSystemTime, so a different clock gives a different stamp", _ {
		// The companion to the example above: vary only the clock. Without this, a `nowMs()` that
		// returned a constant would pass everything above it.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::system_time_s = 0.037; // 37 ms
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true, false);
		expect(r.guest.shell().lowering().held_since(ButtonId::Load)).to_equal(uint32_t(37));

		(void)r.guest.buttonAction(deluge::hid::button::LOAD, false, false);
		fakelegacyui::system_time_s = 9.001; // 9001 ms
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true, false);
		expect(r.guest.shell().lowering().held_since(ButtonId::Load)).to_equal(uint32_t(9001));
	});

	// ---- input: the hooks deliberately NOT wired ------------------------------------------------

	it("leaves all three gold-knob hooks on the base, so legacy's `view` still gets them", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		fakelegacyui::reset();

		// Forwarding these into the lowering would double-accelerate them: legacy has already
		// applied `calcNextKnobSpeed` before it calls (`hid/encoder_input.cpp:192-194`), and the
		// shell applies its own to any `EncoderKind::Accelerating` role
		// (`shell/input_lowering.cpp:187-193`). See the class's `@par The gold knobs`.
		static_cast<::UI&>(r.guest).modEncoderAction(0, 7);
		static_cast<::UI&>(r.guest).modButtonAction(2, true);
		static_cast<::UI&>(r.guest).modEncoderButtonAction(1, true);

		expect(fakelegacyui::mod_encoder_actions).to_equal(1);
		expect(fakelegacyui::mod_button_actions).to_equal(1);
		expect(fakelegacyui::mod_encoder_button_actions).to_equal(1);

		// And nothing reached the shell, on this tick or any later one.
		r.guest.shell().tick(1);
		expect(r.base.seen.empty()).to_be_true();
	});

	// ---- the tick: arming ------------------------------------------------------------------------

	it("arms UI_SPECIFIC when it opens, and arms nothing when the open is refused", _ {
		Rig r;
		expect(r.open()).to_be_true();
		// Nothing above `opened()` armed a deadline, so the ask is the floor rather than a
		// conversion (`ShellHostUI::armTickSoon`).
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));

		// The same guest shape, varying only the one thing the refusal is about: no initial scene,
		// so `Shell::init`'s push is refused and `opened()` returns false. `openUI` then rolls the
		// hierarchy back to the previous UI (`gui/ui/ui.cpp:237-242`) -- an arm left standing here
		// would deliver this guest's `timerCallback` to *that* UI, since the manager dispatches to
		// `getCurrentUI()` (`gui/ui_timer_manager.cpp:143`).
		Rig refused;
		expect(refused.guest.opened()).to_be_false();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(-1));
		expect(fakelegacyui::timer_arms.empty()).to_be_true();
	});

	it("ticks the shell on its own clock and re-arms UI_SPECIFIC to the DURATION until next_wake", _ {
		Rig r;
		r.base.timer = 120; // an ordinary animated scene: "wake me in 120 ms"
		expect(r.open()).to_be_true();

		set_clock_ms(500);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		// The shell reports an ABSOLUTE deadline...
		expect(r.guest.shell().next_wake().has_value()).to_be_true();
		expect(r.guest.shell().next_wake().value_or(0)).to_equal(uint32_t(620));
		// ...and `uiTimerManager` was handed the DURATION. A guest that passed the deadline straight
		// through would have armed 620 ms, and one that passed `now` would have armed 500.
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(120));
		// The tick really ran on `nowMs()`, not on 0: `next_wake` is `now + 120`.
		expect(fakelegacyui::timer_arms.back().which == TimerName::UI_SPECIFIC).to_be_true();
	});

	it("arms NOTHING when it is no longer the current UI, however loudly the shell asks for a wake", _ {
		// **The guard at `ShellHostUI::rearmTimer`'s top** (`gui/ui/shell_host_ui.cpp:620`).
		// `TimerName::UI_SPECIFIC` is the *current* UI's timer -- `UITimerManager::routine` dispatches
		// it to `getCurrentUI()->timerCallback()` and never to whatever armed it
		// (`gui/ui_timer_manager.cpp:142-148`) -- so arming it while something else is current hands
		// this guest's tick to that something else.
		//
		// This is not hypothetical, and it is not a state the guest puts itself into: three of the
		// dispatcher's arms end in `openUI(...)` (`gui/ui/shell_action_dispatch.cpp`), and legacy's
		// `NO_NAVIGATION` semantics correctly leave the ported menu on the shell stack when they do --
		// so `timerCallback`'s `depth() == 0` close does not fire and control reaches `rearmTimer`
		// with a foreign UI on top. `dispatchShellActions` is stubbed in this suite, so the *cause*
		// cannot be reproduced here; the guest's response to the resulting state can, and is exactly
		// what this pins. The end-to-end version, with the real dispatcher opening the real
		// `performanceView`, is `sim/scenes_spec/menu_actions_spec.cpp`'s
		// `menu_action_three_verbs`.
		Rig r;
		// A scene that wants a wake on every tick, so "nothing was armed" cannot be the shell simply
		// having nothing to ask for -- `next_wake()` answering `nullopt` takes a different early
		// return (`gui/ui/shell_host_ui.cpp:625-640`) and would make this example vacuous.
		r.base.timer = 120;
		expect(r.open()).to_be_true();

		// The control leg, and it is what makes the subject mean anything: still current, so the arm
		// happens. Same rig, same scene, same clock -- one thing differs below.
		set_clock_ms(500);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(120));

		// Now something else is the current UI -- the state an arm ending in `openUI(...)` leaves.
		fakelegacyui::timer_arms.clear();
		OtherUI someone_else;
		fakelegacyui::current_ui = &someone_else;
		set_clock_ms(600);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		// The shell still wants a wake -- so the refusal is the guard's doing and not an empty shell.
		expect(r.guest.shell().next_wake().has_value()).to_be_true();
		expect(fakelegacyui::timer_arms.empty()).to_be_true();

		// And it recovers the moment the guest is current again, which is the half that says the
		// guard is not a wedge. In production `closeUI` does this through `focusRegained`
		// (`gui/ui/ui.cpp:197-199`); here the topology is declared rather than simulated.
		fakelegacyui::current_ui = &r.guest;
		set_clock_ms(700);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(120));
	});

	it("arms the floor -- not zero -- for a deadline that is already now, and does not wedge there", _ {
		Rig r;
		// `Scene::next_timer()` may legally answer 0, meaning "next tick"
		// (`shell/scene.h:510-562`); `AudioClipScene` answers it for the whole of playback
		// (`scenes/audio_clip/audio_clip_scene.cpp:1197-1203`). `next_wake()` is then `now_ms` on
		// every tick, for ever.
		r.base.timer = 0;
		expect(r.open()).to_be_true();

		set_clock_ms(500);
		(void)r.guest.timerCallback();
		// The deadline really is `now`, so the clamp is what is being tested and not an accident of
		// the deadline being one millisecond away.
		expect(r.guest.shell().next_wake().value_or(0)).to_equal(uint32_t(500));
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));

		// `setTimer(UI_SPECIFIC, 0)` would put `triggerTime` at exactly the audio sample timer, which
		// the audio engine leaves behind before `UITimerManager::routine` is next called at all -- so
		// the whole timer sweep, and the shell's whole tick, would run on every pass of the main loop
		// for as long as this scene is stacked. 1 ms is the smallest duration `setTimer`'s `ms * 44`
		// can express that leaves `triggerTime` strictly in the future. The floor is what stops that;
		// the chain must still make progress.
		uint32_t now = 500;
		const std::vector<uint32_t> fired = run_quiet(r.guest, now, 4);
		expect(fired.size()).to_equal(size_t(4));
		expect(nth_or(fired, 0, uint32_t(0))).to_equal(uint32_t(501));
		expect(nth_or(fired, 3, uint32_t(0))).to_equal(uint32_t(504));
	});

	it("clamps a far-future deadline, because setTimer computes ms * 44 into an int32_t", _ {
		// A deadline inside the cap is armed exactly, so the clamp is not just "always the cap".
		Rig near;
		near.base.timer = 40'000'000; // ~11 hours
		expect(near.open()).to_be_true();
		set_clock_ms(500);
		(void)near.guest.timerCallback();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(40'000'000));

		// Past it, the arm saturates. Unclamped, `ms * 44` is 4.4e9 and overflows
		// (`gui/ui_timer_manager.cpp:253-255`); waking early instead costs one tick, because the
		// next tick recomputes `next_wake()` from scratch.
		Rig far;
		far.base.timer = 100'000'000; // ~27 hours
		expect(far.open()).to_be_true();
		set_clock_ms(500);
		(void)far.guest.timerCallback();
		expect(far.guest.shell().next_wake().value_or(0)).to_equal(uint32_t(100'000'500));
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMaxRearmMs));
		expect(int64_t(ShellHostUI::kMaxRearmMs) * ShellHostUI::kTimerSamplesPerMs <= int64_t(2147483647))
		    .to_be_true();
	});

	it("arms nothing when the shell wants nothing, and input arms it again", _ {
		Rig r; // no scene timer, no declared LED, nothing animated
		expect(r.open()).to_be_true();

		uint32_t now = 0;
		const std::vector<uint32_t> fired = run_quiet(r.guest, now, 5);
		// One tick ran (the one `opened()` asked for) and then the chain stopped, because there was
		// nothing left to wake for. That is "sleep until the next input", not a freeze.
		expect(fired.size()).to_equal(size_t(1));
		expect(r.guest.shell().next_wake().has_value()).to_be_false();
		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// And the guest is not wedged: a press re-arms it, which is the only reason a press ever
        // reaches a scene -- routing happens in `Shell::pump_input`, which only `tick()` calls.
		expect(r.guest.padAction(3, 4, 200)).to_equal(ActionResult::DEALT_WITH);
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));
		expect(r.base.seen.empty()).to_be_true(); // nothing routed yet: the tick has not run
		const std::vector<uint32_t> after = run_quiet(r.guest, now, 2);
		expect(after.empty()).to_be_false();
		expect(first_of(r.base, UiEvent::Kind::Pad) != nullptr).to_be_true();
	});

	it("re-arms on focusRegained, because closeUI clears UI_SPECIFIC one line before calling it", _ {
		Rig r;
		expect(r.open()).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 3);
		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// What legacy does when a UI opened over the guest closes again: `closeUI` unsets the timer
		// (`gui/ui/ui.cpp:197`) and then hands focus back (`:199`). Without the re-arm the guest
		// would tick normally until anything was opened over it and never again.
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 250); // the guest was mid-animation
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		r.guest.focusRegained();

		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));
	});

	it("asks for a tick on displayOrLanguageChanged, which boot() alone cannot", _ {
		Rig r;
		expect(r.open()).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 3);
		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// `Shell::boot()` calls `request_repaint()`, which fires a gate but arms no deadline
		// (`shell/shell.cpp:230-239`), so `compute_next_wake` has nothing to report and the repaint
        // would wait for an unrelated wake that never comes.
		r.guest.displayOrLanguageChanged();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));

		// And that tick really does repaint: legacy's buffer, blanked first, comes back non-blank.
		OLED::main.clear();
		(void)fakelegacy::consume_oled_frame();
		(void)run_quiet(r.guest, now, 1);
		expect(any_ink(OLED::main.bytes())).to_be_true();
	});

	it("arms on exitUI when the guest survives the pop, and not when it closes", _ {
		Rig survive;
		expect(survive.open()).to_be_true();
		expect(survive.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(survive.guest, now, 3);
		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// Reached from `TimerName::BACK_MENU_EXIT` (`gui/ui_timer_manager.cpp:149-155`), not from
		// `UI_SPECIFIC`, so nothing has re-armed the tick and the stack has just changed.
		expect(survive.guest.exitUI()).to_equal(ActionResult::DEALT_WITH);
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));

		// The close path, varying only the stack depth. `close()` is `closeUI(this)`, which unsets
		// the timer itself (`gui/ui/ui.cpp:197`) -- so arming here would be undone one line later,
		// or worse, handed to the UI beneath.
		Rig closing;
		expect(closing.open()).to_be_true();
		(void)run_quiet(closing.guest, now, 3);
		fakelegacyui::timer_arms.clear();
		expect(closing.guest.exitUI()).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::timer_arms.empty()).to_be_true();
	});

	// ---- the tick: the SD-routine release, made time-driven ---------------------------------------

	it("keeps polling while the SD lock is held, so a deferred press is not stranded", _ {
		Rig r;
		expect(r.open()).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 3);

		fakelegacyui::sd_routine_active = true;
		expect(r.guest.padAction(3, 4, 200)).to_equal(ActionResult::DEALT_WITH);
		(void)run_quiet(r.guest, now, 3);
		// Nothing routed -- the press is in the lowering's deferred queue
		// (`shell/input_lowering.cpp:120-131`) -- and yet the chain is still alive, because the
		// lock itself is the thing being waited on and no scene can express it as a deadline.
		expect(r.base.seen.empty()).to_be_true();
		expect(r.guest.shell().lowering().sd_busy()).to_be_true();
		expect(r.guest.shell().next_wake().has_value()).to_be_false();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(ShellHostUI::kMinRearmMs));

		// The routine ends. NO further input: the release is time-driven now.
		fakelegacyui::sd_routine_active = false;
		(void)run_quiet(r.guest, now, 2);
		expect(first_of(r.base, UiEvent::Kind::Pad) != nullptr).to_be_true();
	});

	it("never answers REMIND_ME_OUTSIDE_CARD_ROUTINE, which would re-fire on a past triggerTime", _ {
		Rig r;
		expect(r.open()).to_be_true();
		// The value the `UI_SPECIFIC` arm acts on: it sets `active` back to true WITHOUT touching
		// `triggerTime` (`gui/ui_timer_manager.cpp:144-146`), which is already past -- so the timer
		// re-fires on the manager's very next pass, for ever.
		fakelegacyui::sd_routine_active = true;
		set_clock_ms(10);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);
		fakelegacyui::sd_routine_active = false;
		set_clock_ms(20);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);
	});

	// ---- the tick: LEDs, and the ownership rule in both directions --------------------------------

	it("blinks a declared LED on schedule with no input at all", _ {
		Rig r;
		// `LedId`'s values ARE `ButtonId`'s (`shell/profiles/deluge.h:19`), and the profile maps
		// `Back` to board LED 16 -- cross-checked here against legacy's own enum rather than
		// hardcoded, so a profile that renumbered would fail here rather than pass silently.
		const uint8_t back = static_cast<uint8_t>(IndicatorLED::BACK);
		expect(back).to_equal(uint8_t(16));
		r.base.led_declared = true;
		r.base.led_id = static_cast<deluge::ui::LedId>(ButtonId::Back);
		// The same shape `MenuScene::leds` declares for BACK and LEARN
		// (`scenes/menu/menu_scene.cpp:1130`, `:1137`): a plain blink, no deadline.
		r.base.led_state = deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, 128, 0};
		expect(r.open()).to_be_true();

		// The whole chain, driven by nothing but the guest's own arms.
		uint32_t now = 0;
		const std::vector<uint32_t> fired = run_quiet(r.guest, now, 5);

		// Each wake lands on the LED's own half-period boundary (`led_next_change`,
		// `shell/leds.cpp:35-59`), not on some unrelated cadence that happens to divide it.
		expect(fired.size()).to_equal(size_t(5));
		expect(nth_or(fired, 0, uint32_t(0))).to_equal(uint32_t(1));   // the arm `opened()` asked for
		expect(nth_or(fired, 1, uint32_t(0))).to_equal(uint32_t(128)); // ...and then the blink is the only wake source
		expect(nth_or(fired, 2, uint32_t(0))).to_equal(uint32_t(256));
		expect(nth_or(fired, 3, uint32_t(0))).to_equal(uint32_t(384));
		expect(nth_or(fired, 4, uint32_t(0))).to_equal(uint32_t(512));

		// And the LED actually changed state on those boundaries, through
		// `indicator_leds::setLedState`. The first tick sends it lit (`sent_once` was false), then
		// one write per flip and no writes in between -- `Shell::flush` diffs
		// (`shell/leds.cpp:75-80`).
		const std::vector<bool> history = led_history(back);
		expect(history.size()).to_equal(size_t(5));
		expect(nth_or(history, 0, !true)).to_be_true();
		expect(nth_or(history, 1, !false)).to_be_false();
		expect(nth_or(history, 2, !true)).to_be_true();
		expect(nth_or(history, 3, !false)).to_be_false();
		expect(nth_or(history, 4, !true)).to_be_true();
	});

	it("owns only the LEDs a scene declares, in both directions", _ {
		Rig r;
		const uint8_t back = static_cast<uint8_t>(IndicatorLED::BACK);
		r.base.led_declared = true;
		r.base.led_id = static_cast<deluge::ui::LedId>(ButtonId::Back);
		r.base.led_state = deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, 128, 0};
		expect(r.open()).to_be_true();

		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 5);
		expect(led_history(back).size()).to_equal(size_t(5));

		// **Nothing else was touched.** Every other board LED is legacy's while the guest is open,
		// and an undeclared id cannot pass `led_send_needed`'s `desired_declared` test however many
		// ticks run (`shell/leds.cpp:75-80`). PLAY is the sharp one: legacy drives it from the
		// transport all the way through a menu.
		for (const fakelegacy::LedWrite& w : fakelegacy::led_writes) {
			expect(w.led).to_equal(back);
		}
		expect(led_history(static_cast<uint8_t>(IndicatorLED::PLAY)).empty()).to_be_true();

		// **And the other direction: an LED the scene stops declaring goes back to legacy.** Only the
		// declaration is varied -- same scene, same clock, same blink. The arm the fifth tick left
		// standing is the blink's own next boundary (640 = 512 + 128), so the next tick lands exactly
		// where a guest that still thought it owned the LED would write the flip. Nothing is re-armed
		// by hand here: that the chain continues at all is part of what is being asserted.
		const size_t before = fakelegacy::led_writes.size();
		r.base.led_declared = false;
		const std::vector<uint32_t> after = run_quiet(r.guest, now, 4);

		// **Exactly ONE further tick runs, and that is the mechanism rather than a limitation.** With
		// nothing declared the composed `LedFrame` is empty, so `compute_next_wake`'s blink loop
		// (`shell/shell.cpp:1113-1117`) has no source, `next_wake()` is `std::nullopt` and the chain
		// ends -- which is itself the ownership rule showing through the wake computation. One tick
		// is all this assertion needs: it is the tick on which the flip would have been written.
		expect(after.size()).to_equal(size_t(1));
		expect(nth_or(after, 0, uint32_t(0))).to_equal(uint32_t(640)); // 512 + 128, a phase boundary
		expect(fakelegacy::led_writes.size()).to_equal(before);
	});

	it("writes a knob indicator only when something declares one -- which nothing does in phase 4", _ {
		Rig r;
		r.base.led_declared = true;
		r.base.led_id = static_cast<deluge::ui::LedId>(ButtonId::Back);
		r.base.led_state = deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, 128, 0};
		expect(r.open()).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 5);

		// `Shell::flush` calls `set_indicator` only for an indicator declared this tick
		// (`shell/shell.cpp:1032-1044`), and the only declarer in the tree is `ModKnobs`
		// (`scenes/services/mod_knobs.cpp:687`), which is a *service* -- and this guest is built
		// with `Services{}`. So legacy's `knobIndicatorLevels[]` shadow, which
		// `GuestSurfaceSet::set_indicator` cannot keep true, cannot go stale until F1 task 8
		// installs that service. This example is what makes that a measurement rather than a
		// reading of the code: the LED writes above prove the tick ran.
		expect(fakelegacy::indicator_writes.empty()).to_be_true();
		expect(fakelegacy::led_writes.empty()).to_be_false();

		// **And the other half, so the claim is about the declaration and not about the guest never
		// writing indicators at all.** Installing a service that declares one -- which is exactly
		// what F1 task 8 does with `ModKnobs`, through the `Shell::set_services` F1 task 2 built for
		// it -- makes the very next tick write it. That write is the moment legacy's
		// `knobIndicatorLevels[]` shadow starts diverging from the hardware, and
		// `GuestSurfaceSet::set_indicator` cannot update it, so the guest's close path will need
		// `indicator_leds::clearKnobIndicatorLevels()` (`hid/led/indicator_leds.h:88`).
		//
		// This example cannot fail when task 8 lands -- it builds its own `Services` and the real
		// installation happens in `deluge.cpp` -- so it is a demonstration of the mechanism and the
		// hand-over, not a guard. The guard belongs in task 8's own spec, and this is the shape it
		// has to cover.
		IndicatorService svc;
		r.guest.shell().set_services(Services{.mod_knobs = &svc});
		expect(run_quiet(r.guest, now, 1).size()).to_equal(size_t(1));
		expect(fakelegacy::indicator_writes.size()).to_equal(size_t(1));
		// Read through a fallback for the same reason as `nth_or` above: a mutation that stops the
		// chain leaves this vector empty, and an out-of-range read is undefined behaviour rather than
		// a failed expectation.
		const fakelegacy::IndicatorWrite got =
		    fakelegacy::indicator_writes.empty() ? fakelegacy::IndicatorWrite{} : fakelegacy::indicator_writes[0];
		expect(got.which).to_equal(uint8_t(0)); // IndicatorId::Lower
		expect(got.count).to_equal(uint8_t(4));
		expect(got.levels[2]).to_equal(uint8_t(0x5A));
	});

});
// clang-format on

DELUGE_SPEC(gui_shell_host_ui_spec, shell_host_ui)
