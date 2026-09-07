#include "support/fake_legacy_hid.h"
#include "support/fake_legacy_ui.h"

#include "gui/ui/shell_host_ui.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "shell/profiles/deluge.h"
#include "ui/core/style.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "../support/spec_main.h"

using deluge::gui::ui::ShellHostUI;
using deluge::hid::display::OLED;
using namespace deluge::ui::shell;

namespace {

constexpr SceneKind kBase = SceneKind::FirstApp;
constexpr SceneKind kOver = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 1);

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

	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

	bool is_grid_overlay() const override { return overlay; }
	bool occludes_grid() const override { return occludes; }
	bool dims_grid() const override { return false; }
	bool input_transparent() const override { return passive; }
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return greyout; }

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
struct Rig {
	PatternScene base{kBase};
	PatternScene over{kOver};
	std::array<Scene*, 18> table{};
	SceneRegistry reg{std::span<Scene* const>{table}};
	ShellHostUI guest;

	Rig() : guest(fill(), kDelugeBoard, deluge_profile()) {
		fakelegacy::reset();
		fakelegacyui::reset();
	}
	explicit Rig(const DelugeBoard& board, const BoardProfile& profile) : guest(fill(), board, profile) {
		fakelegacy::reset();
		fakelegacyui::reset();
	}
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
		// and `Scene::on_resume`'s contract is about the latter (`shell/scene.h:311-326`).
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
		// `changed_since_last_query()` before it renders (`shell/shell.cpp:328-340`). The `boot()`
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
		// `is_grid_overlay()` (`scenes/menu/menu_scene.h:441-442`).
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

		// `MenuScene::occludes_grid()` is false (`scenes/menu/menu_scene.h:443-444`), and
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
		// (`scenes/menu/menu_scene.cpp:1458-1490`), and legacy's false means "obey UI under me"
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
		// same thing `SoundEditor::exitCompletely` does at `gui/ui/sound_editor.cpp:723`. Without
		// it the user is trapped: `exitUI`'s only caller stores the result and tests it against
		// `REMIND_ME_OUTSIDE_CARD_ROUTINE` alone (`gui/ui_timer_manager.cpp:149-155`), so a bare
		// `NOT_DEALT_WITH` return closes nothing.
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::closed[0] == static_cast<::UI*>(&r.guest)).to_be_true();
		expect(result == ActionResult::NOT_DEALT_WITH).to_be_true();
	});

	it("is safe on an already-empty stack: it closes once and pops nothing", _ {
		Rig r;
		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		const ActionResult result = r.guest.exitUI();
		expect(result == ActionResult::NOT_DEALT_WITH).to_be_true();
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
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
});
// clang-format on

DELUGE_SPEC(gui_shell_host_ui_spec, shell_host_ui)
