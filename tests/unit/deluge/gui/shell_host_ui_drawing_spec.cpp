#include "support/shell_host_ui_rig.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace shell_host_ui_rig;

// clang-format off
describe shell_host_ui_drawing("gui::ui::ShellHostUI: what it draws into legacy's OLED and pad image", $ {

	// ---- the OLED, which is the hook legacy's own clear makes necessary -------------------------

	it("hands the frame back after legacy clears the panel under it, at the shipped 48-row panel", _ {
		Rig r;
		expect(r.open()).to_be_true();
		// One shell pass. `GuestSurfaceSet::blit` copies the rendered panel into `OLED::main` and
		// marks it changed; that copy is the oracle for what legacy is meant to end up holding.
		//
		// This is also the assertion that `opened()` does not need a `Shell::boot()` of its own
		// the *first* tick after opening must draw, and it does, because `SceneStack::reset` and the
		// push both set `changed_` and `Shell::tick` re-arms every gate on
		// `changed_since_last_query()` before it renders. A `boot()` call in `opened()` would be
		// dead.
		r.guest.shell().tick(0);
		const std::vector<uint8_t> rendered = copy_of(OLED::main.bytes());
		expect(any_ink(rendered)).to_be_true();
		expect(fakelegacy::consume_oled_frame()).to_be_true();

		// Now legacy's own render pass: `doAnyPendingOLEDRendering` clears the image
		// (`gui/ui/ui.cpp`) and then asks each open UI to draw into it. Without this
		// hook the shell would have nothing armed to re-blit and the frame would go out blank.
		OLED::main.clear();
		expect(any_ink(OLED::main.bytes())).to_be_false();
		r.guest.renderOLED(OLED::main);
		expect(copy_of(OLED::main.bytes())).to_equal(rendered);

		// It restores, it does not re-mark: legacy has already decided to send this frame by the
		// time it calls here (`gui/ui/ui.cpp:395`).
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
		expect(copy_of(OLED::main.bytes())).to_equal(shellSide);
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
				expect(PadLEDs::image[y][x]).to_equal(PatternScene::pad_for(x, y));
			}
			// The sidebar is `renderSidebar`'s, and legacy asks the two questions separately.
			expect(PadLEDs::image[y][kDisplayWidth]).to_equal(RGB{0, 0, 0});
			expect(PadLEDs::image[y][kDisplayWidth + 1]).to_equal(RGB{0, 0, 0});
			// The occupancy map is untouched, and that is a stated gap rather than a feature:
			// nothing in the shell populates one (as for `send_columns`),
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
			expect(PadLEDs::image[y][kDisplayWidth]).to_equal(PatternScene::pad_for(kDisplayWidth, y));
			expect(PadLEDs::image[y][kDisplayWidth + 1]).to_equal(PatternScene::pad_for(kDisplayWidth + 1, y));
			expect(PadLEDs::image[y][0]).to_equal(RGB{0, 0, 0});
			expect(PadLEDs::image[y][kDisplayWidth - 1]).to_equal(RGB{0, 0, 0});
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
		// Both of `serveGrid`'s clamps need an example that varies the grid's dimensions -- otherwise
		// both survive mutation, because every board in the file would have legacy's own 16+2 x 8. Unlike `renderOLED`'s size guard (M28), these two are
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
		// `tests/support/cmake/specs.cmake`, so an out-of-bounds write is otherwise silent here.)
		RGB oversized[deluge::gfx::grid::kMaxGridRows][kDisplayWidth + kSideBarWidth]{};

		const bool usedUp = r.guest.renderMainPads(0xFFFFFFFF, oversized, PadLEDs::occupancyMask, true);
		expect(usedUp).to_be_true();

		for (int32_t y = 0; y < kDisplayHeight; y++) {
			// Columns the board actually has: copied.
			for (int32_t x = 0; x < 10; x++) {
				expect(oversized[y][x]).to_equal(PatternScene::pad_for(x, y));
			}
			// Columns it does not: left alone, rather than read out of the grid's range.
			for (int32_t x = 10; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(oversized[y][x]).to_equal(RGB{0, 0, 0});
			}
		}
		// Rows past legacy's eight: never written, however many rows the board's grid has. This is
		// the assertion that would have caught a write past the end of `PadLEDs::image`.
		for (int32_t y = kDisplayHeight; y < deluge::gfx::grid::kMaxGridRows; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(oversized[y][x]).to_equal(RGB{0, 0, 0});
			}
		}
	});

	it("answers the opacity query without drawing, which is what the no-image form is for", _ {
		Rig r;
		r.base.occludes = true;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		blank_legacy_pads();

		// `UI`'s contract: quizzable without any rendering (`UI::renderMainPads`). Legacy asks it
		// this way in `uiNeedsRendering` (`gui/ui/ui.cpp`) and in `closeUI`
		// (`:186-187`).
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_true();
		expect(static_cast<::UI&>(r.guest).renderSidebar()).to_be_true();
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(PadLEDs::image[y][x]).to_equal(RGB{0, 0, 0});
			}
		}
	});

	it("returns the stack's opacity per region, in both directions", _ {
		Rig r;
		expect(r.open()).to_be_true();

		// A scene that draws no pads is transparent over them, which is what `SoundEditor` is: it
		// does not override either hook, so it takes `UI`'s `return false`, and `MenuScene` keeps
		// `Scene::pad_regions`'s `None` for a grid overlay.
		r.base.regions = PadRegions::None;
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_false();
		expect(static_cast<::UI&>(r.guest).renderSidebar()).to_be_false();

		// A scene that draws every pad owns both regions.
		r.base.regions = PadRegions::All;
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_true();
		expect(static_cast<::UI&>(r.guest).renderSidebar()).to_be_true();

		// A scene that draws nothing over one that draws everything leaves them owned: the shell
		// composes the overlay onto that scene's frame (`Shell::render_grid` from `grid_base()`).
		r.over.overlay = true;
		r.over.regions = PadRegions::None;
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_true();

		// Only when no scene draws a region is the guest transparent over it.
		r.base.regions = PadRegions::None;
		expect(static_cast<::UI&>(r.guest).renderMainPads()).to_be_false();
	});

	it("serves the main pads and leaves the sidebar to the UI underneath under a Main-only scene", _ {
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::Main; // a keyboard: `QwertyUI::drawKeys` draws main pads only
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				PadLEDs::image[y][x] = RGB{9, 8, 7}; // the root view's pads
			}
		}

		expect(r.guest.renderMainPads(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask, true)).to_be_true();
		expect(r.guest.renderSidebar(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask)).to_be_false();
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				expect(PadLEDs::image[y][x]).to_equal(PatternScene::pad_for(x, y));
			}
			expect(PadLEDs::image[y][kDisplayWidth]).to_equal(RGB{9, 8, 7});
			expect(PadLEDs::image[y][kDisplayWidth + 1]).to_equal(RGB{9, 8, 7});
		}
		expect(fakelegacyui::marker_sidebar_requests).to_equal(0);
	});

	it("draws the clip view's sidebar under a marker over the keyboard screen", _ {
		// `SampleMarkerEditor::renderSidebar` (`gui/ui/sample_marker_editor.cpp:875-881`): over the
		// keyboard screen, `instrumentClipView`'s sidebar; over any other root, the UI underneath's.
		Rig r;
		r.marker.overlay = true;
		r.marker.regions = PadRegions::Main;
		expect(r.open(deluge::scenes::marker::kMarkerSceneKind)).to_be_true();

		fakelegacyui::marker_sidebar_answer = true;
		expect(r.guest.renderSidebar(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask)).to_be_true();
		expect(fakelegacyui::marker_sidebar_requests).to_equal(1);

		fakelegacyui::marker_sidebar_answer = false;
		expect(r.guest.renderSidebar(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask)).to_be_false();
		expect(fakelegacyui::marker_sidebar_requests).to_equal(2);
	});

	// ---- pads under an overlay-only stack: the root view's, as under `SoundEditor` ----------------

	it("leaves legacy's pad image to the UI underneath while no scene owns the grid", _ {
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		expect(r.open()).to_be_true();
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				PadLEDs::image[y][x] = RGB{9, 8, 7}; // the root view's pads
			}
		}
		r.guest.surfaces().grid().gate.request_repaint();
		r.guest.shell().tick(0);
		// `SoundEditor::renderMainPads` answers false ("show root UI") and writes nothing; it has no
		// `renderSidebar` (`UI`'s default false).
		expect(static_cast<::UI&>(r.guest).renderMainPads(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask))
		    .to_be_false();
		expect(static_cast<::UI&>(r.guest).renderSidebar(0xFFFFFFFF, PadLEDs::image, PadLEDs::occupancyMask))
		    .to_be_false();
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(PadLEDs::image[y][x]).to_equal(RGB{9, 8, 7});
			}
		}
		expect(fakelegacy::column_sends.empty()).to_be_true();
	});

	it("carries a scene's flash to PadLEDs::flashMainPad while it owns no pads", _ {
		using deluge::ui::grid::FlashColour;
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		r.base.sparse = true;
		expect(r.open()).to_be_true();
		const auto tick = [&](uint32_t now) {
			r.guest.surfaces().grid().gate.request_repaint();
			r.guest.shell().tick(now);
		};
		tick(0);
		expect(fakelegacy::pad_flashes.empty()).to_be_true();

		// `SoundEditor::blinkShortcut` flashes through `PadLEDs::flashMainPad`, index
		// `y + x * kDisplayHeight`, with the colour code it was set up with
		// (`sound_editor.cpp:1205-1245`).
		r.base.flashes = {{3, 2, FlashColour::Default}, {14, 5, FlashColour::Yellow}};
		tick(10);
		const fakelegacy::PadFlash none{255, -1};
		expect(fakelegacy::pad_flashes.size()).to_equal(size_t(2));
		expect(nth_or(fakelegacy::pad_flashes, 0, none).idx).to_equal(uint8_t(2 + 3 * kDisplayHeight));
		expect(nth_or(fakelegacy::pad_flashes, 0, none).colour).to_equal(int32_t(0));
		expect(nth_or(fakelegacy::pad_flashes, 1, none).idx).to_equal(uint8_t(5 + 14 * kDisplayHeight));
		expect(nth_or(fakelegacy::pad_flashes, 1, none).colour).to_equal(int32_t(0b11));

		// A flash is an event, not a level: a render that flashes nothing sends nothing.
		r.base.flashes.clear();
		tick(20);
		expect(fakelegacy::pad_flashes.size()).to_equal(size_t(2));
		expect(fakelegacy::column_sends.empty()).to_be_true();
	});

	it("on opening over an overlay-only stack, asks legacy to draw the root's pads and re-reads the greyout", _ {
		// `SoundEditor::opened` calls `uiNeedsRendering(this, 0xFFFFFFFF, 0)` (`SoundEditor::opened`),
		// and `openUI` re-reads the greyout before `opened()` -- when the guest's stack is still empty --
		// so the guest re-reads it once its scene is on.
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		r.base.greyout = deluge::ui::grid::GreyoutMask{0xFFFFFFFE, 0};
		expect(r.open()).to_be_true();
		const fakelegacyui::RenderRequest none{nullptr, 0, 0xDEAD};
		expect(fakelegacyui::render_requests.size()).to_equal(size_t(1));
		expect(nth_or(fakelegacyui::render_requests, 0, none).ui).to_equal(&r.guest);
		expect(nth_or(fakelegacyui::render_requests, 0, none).main_rows).to_equal(uint32_t(0xFFFFFFFF));
		expect(nth_or(fakelegacyui::render_requests, 0, none).side_rows).to_equal(uint32_t(0));
		expect(fakelegacy::greyout_reassessments).to_equal(1);
	});

	it("on refreshing the root facts, re-reads them and then the greyout they decide", _ {
		// The facts change under an open menu when the root or the automation view's editor does; the greyout the
		// menu answers moves with them, and legacy re-reads it on every greyout.
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		r.base.greyout = deluge::ui::grid::GreyoutMask{0xFFFFFFFC, 0};
		expect(r.open()).to_be_true();
		const int reassessments = fakelegacy::greyout_reassessments;
		r.base.greyout = {};
		r.guest.refreshRootFacts();
		expect(fakelegacyui::root_fact_refreshes).to_equal(1);
		expect(fakelegacy::greyout_reassessments).to_equal(reassessments + 1);
	});

	it("greys at once when a scene takes the main pads and greys every column, as the SHIFT overlay does", _ {
		// `renderMainShortcutsOnly` calls `PadLEDs::doGreyoutInstantly()`: the overlay's greyout does not fade in.
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		r.base.greyout = {};
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		const int instant = fakelegacy::instant_greyout_reassessments;
		r.base.regions = PadRegions::Main;
		r.base.greyout = {0xFFFFFFFFu, 0};
		set_clock_ms(10);
		(void)r.guest.timerCallback();
		expect(fakelegacy::instant_greyout_reassessments).to_equal(instant + 1);
	});

	it("takes every pad when a scene comes to own the grid, and hands them back when it goes", _ {
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		r.base.sparse = true;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				PadLEDs::image[y][x] = RGB{9, 8, 7}; // the root view's pads
			}
		}
		const int reassessed = fakelegacy::greyout_reassessments;

		// A scene that owns the pads but paints them all black: no pair of the shell's grid changes,
		// and still every pad must leave the root view's colours.
		r.over.sparse = true;
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		set_clock_ms(10);
		(void)r.guest.timerCallback();
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(PadLEDs::image[y][x]).to_equal(RGB{0, 0, 0});
			}
		}
		expect(fakelegacy::greyout_reassessments).to_equal(reassessed + 1);

		// And back: the UI underneath is asked to draw every pad again.
		r.guest.shell().stack().pop(kOver);
		const size_t requests = fakelegacyui::render_requests.size();
		set_clock_ms(20);
		(void)r.guest.timerCallback();
		const fakelegacyui::RenderRequest none{nullptr, 0, 0};
		expect(fakelegacyui::render_requests.size()).to_equal(requests + 1);
		expect(nth_or(fakelegacyui::render_requests, requests, none).main_rows).to_equal(uint32_t(0xFFFFFFFF));
		expect(nth_or(fakelegacyui::render_requests, requests, none).side_rows).to_equal(uint32_t(0xFFFFFFFF));
		expect(fakelegacy::greyout_reassessments).to_equal(reassessed + 2);
	});

	it("takes the main pads, then asks the UI underneath for just the half it released", _ {
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		r.base.sparse = true;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		const fakelegacyui::RenderRequest none{nullptr, 0xDEAD, 0xDEAD};

		// A keyboard: the main pads only. Released, only they are asked for again.
		r.over.overlay = true;
		r.over.regions = PadRegions::Main;
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		set_clock_ms(10);
		(void)r.guest.timerCallback();
		r.guest.shell().stack().pop(kOver);
		size_t requests = fakelegacyui::render_requests.size();
		set_clock_ms(20);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::render_requests.size()).to_equal(requests + 1);
		expect(nth_or(fakelegacyui::render_requests, requests, none).ui).to_equal(&r.guest);
		expect(nth_or(fakelegacyui::render_requests, requests, none).main_rows).to_equal(uint32_t(0xFFFFFFFF));
		expect(nth_or(fakelegacyui::render_requests, requests, none).side_rows).to_equal(uint32_t(0));

		// The sidebar alone, released alone.
		r.over.regions = PadRegions::Sidebar;
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		set_clock_ms(30);
		(void)r.guest.timerCallback();
		r.guest.shell().stack().pop(kOver);
		requests = fakelegacyui::render_requests.size();
		set_clock_ms(40);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::render_requests.size()).to_equal(requests + 1);
		expect(nth_or(fakelegacyui::render_requests, requests, none).ui).to_equal(&r.guest);
		expect(nth_or(fakelegacyui::render_requests, requests, none).main_rows).to_equal(uint32_t(0));
		expect(nth_or(fakelegacyui::render_requests, requests, none).side_rows).to_equal(uint32_t(0xFFFFFFFF));
	});

	it("hands the main pads back when the guest closes while it owns them", _ {
		// `closeUI` asks the closing guest whether it drew the pads (`gui/ui/ui.cpp:233-241`), and by
		// then its stack is empty and answers no -- so the guest itself must ask for the root view's
		// main pads, or the keyboard's last frame stays on them.
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::Main;
		r.base.remove_self_on_event = true;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		fakelegacyui::reset();

		(void)r.guest.buttonAction(deluge::hid::button::BACK, true);
		(void)r.guest.timerCallback();

		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		bool mainAsked = false;
		for (const fakelegacyui::RenderRequest& q : fakelegacyui::render_requests) {
			mainAsked = mainAsked || (q.ui == &r.guest && q.main_rows == 0xFFFFFFFF && q.side_rows == 0);
		}
		expect(mainAsked).to_be_true();
		// Before `closeUI`: legacy's `uiNeedsRendering` files rows only for a UI it still finds on
		// its stack (`gui/ui/ui.cpp:336-360`), so a request after the close would be dropped.
		expect(fakelegacyui::ui_calls.size()).to_equal(size_t(2));
		expect(nth_or(fakelegacyui::ui_calls, 0, fakelegacyui::UiCall::Close)).to_equal(fakelegacyui::UiCall::Render);
		expect(nth_or(fakelegacyui::ui_calls, 1, fakelegacyui::UiCall::Render)).to_equal(fakelegacyui::UiCall::Close);
	});

	it("asks for the sidebar when a marker comes to answer for it, and again when it goes", _ {
		// `SampleMarkerEditor::opened` asks for the sidebar over any root but the clip view
		// (`gui/ui/sample_marker_editor.cpp:133-135`): its `renderSidebar` answers for the root
		// (`:875-881`), so the sidebar must be drawn again through it.
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		const fakelegacyui::RenderRequest none{nullptr, 0xDEAD, 0xDEAD};

		r.marker.overlay = true;
		r.marker.regions = PadRegions::Main;
		expect(r.guest.shell().stack().push(SceneId{deluge::scenes::marker::kMarkerSceneKind, 0})).to_be_true();
		size_t requests = fakelegacyui::render_requests.size();
		set_clock_ms(10);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::render_requests.size()).to_equal(requests + 1);
		expect(nth_or(fakelegacyui::render_requests, requests, none).ui).to_equal(&r.guest);
		expect(nth_or(fakelegacyui::render_requests, requests, none).main_rows).to_equal(uint32_t(0));
		expect(nth_or(fakelegacyui::render_requests, requests, none).side_rows).to_equal(uint32_t(0xFFFFFFFF));

		// Gone: its main pads and the sidebar it answered for are both the root's again.
		r.guest.shell().stack().pop(deluge::scenes::marker::kMarkerSceneKind);
		requests = fakelegacyui::render_requests.size();
		set_clock_ms(20);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::render_requests.size()).to_equal(requests + 1);
		expect(nth_or(fakelegacyui::render_requests, requests, none).main_rows).to_equal(uint32_t(0xFFFFFFFF));
		expect(nth_or(fakelegacyui::render_requests, requests, none).side_rows).to_equal(uint32_t(0xFFFFFFFF));
	});

	it("hands the marker's sidebar back when the guest closes under it", _ {
		Rig r;
		r.marker.overlay = true;
		r.marker.regions = PadRegions::Main;
		r.marker.remove_self_on_event = true;
		expect(r.open(deluge::scenes::marker::kMarkerSceneKind)).to_be_true();
		r.guest.shell().tick(0);
		fakelegacyui::reset();

		(void)r.guest.buttonAction(deluge::hid::button::BACK, true);
		(void)r.guest.timerCallback();

		const fakelegacyui::RenderRequest none{nullptr, 0xDEAD, 0xDEAD};
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::render_requests.size()).to_equal(size_t(1));
		expect(nth_or(fakelegacyui::render_requests, 0, none).main_rows).to_equal(uint32_t(0xFFFFFFFF));
		expect(nth_or(fakelegacyui::render_requests, 0, none).side_rows).to_equal(uint32_t(0xFFFFFFFF));
	});

	// ---- canSeeViewUnderneath, which is NOT the OLED question --------------------------------

	it("reports the view underneath as visible unless the top scene occludes the grid", _ {
		Rig r;
		expect(r.open()).to_be_true();

		// `MenuScene::occludes_grid()` is false (`scenes/menu/menu_scene.h`), and
		// `SoundEditor::canSeeViewUnderneath()` is true (`gui/ui/sound_editor.h:121`) -- the guest
		// has to land on legacy's answer, because `UI::graphicsRoutine` runs the root view's
		// animation only while it does (`gui/ui/ui.cpp:76-80`).
		r.base.occludes = false;
		expect(r.guest.canSeeViewUnderneath()).to_be_true();

		r.base.occludes = true;
		expect(r.guest.canSeeViewUnderneath()).to_be_false();

		// It is emphatically not `oled_transparent(Main)`, which this scene never changes: legacy
		// keeps OLED transparency in the `oledShowsUIUnderneath` member instead (`gui/ui/ui.h`),
		// and `Slicer` holds the two at opposite values on one object (`gui/ui/slicer.h:33-38`).
		expect(r.guest.oledShowsUIUnderneath).to_be_false();
	});

	// ---- greyout ------------------------------------------------------------------------------

	it("forwards the stacked scenes' greyout mask, and obeys the UI underneath for an empty one", _ {
		Rig r;
		r.base.overlay = true; // a menu: it draws no pads, so legacy greys the root's
		r.base.regions = PadRegions::None;
		expect(r.open()).to_be_true();

		// `MenuScene::greyout_mask` spells legacy's `doGreyout == false` as an all-zero mask
		// (`menu::greyout_mask`, `scenes/menu/pads.cpp`), and legacy's false means "obey UI under me"
		// (`gui/ui/ui.h:136-138`). The two out-parameters are accumulators the caller zeroed and
		// the next UI down is about to be asked with (`gui/ui/ui.cpp:103-113`), so a false answer must
		// leave them alone.
		r.base.greyout = {};
		uint32_t cols = 0xDEADBEEF;
		uint32_t rows = 0xFEEDFACE;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_false();
		expect(cols).to_equal(uint32_t(0xDEADBEEF));
		expect(rows).to_equal(uint32_t(0xFEEDFACE));

		// A non-zero mask is legacy's `doGreyout == true`, and both halves have to arrive: cols and
		// rows are separate masks that OR into a cross, not a rectangle (`ui/grid/effects.h`).
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

		// A scene above with an empty mask obeys the one beneath, as legacy's top-down walk does
		// (`gui/ui/ui.cpp:103-113`): a popup over the menu leaves the menu's greyout standing.
		r.over.overlay = true;
		r.over.regions = PadRegions::None;
		r.over.greyout = {};
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		cols = 0;
		rows = 0;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_true();
		expect(cols).to_equal(uint32_t(0));
		expect(rows).to_equal(uint32_t(0x00000080));

		// One with a mask of its own replaces the menu's whole, rows included -- not an OR.
		r.over.greyout = {0x0000000Fu, 0u};
		cols = 0;
		rows = 0;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_true();
		expect(cols).to_equal(uint32_t(0x0000000F));
		expect(rows).to_equal(uint32_t(0));
	});

	it("answers the topmost non-empty greyout, not the OR: 0b11 over a menu's 0xFFFFFFFE", _ {
		// `RenameUI::getGreyoutColsAndRows` (`gui/ui/rename/rename_ui.cpp`) over
		// `SoundEditor`'s clip-minder arm.
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		r.base.greyout = {0xFFFFFFFEu, 0};
		expect(r.open()).to_be_true();
		r.over.overlay = true;
		r.over.regions = PadRegions::Main;
		r.over.greyout = {0b11u, 0};
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();

		uint32_t cols = 0;
		uint32_t rows = 0;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_true();
		expect(cols).to_equal(uint32_t(0b11));
		expect(rows).to_equal(uint32_t(0));
	});

	it("a menu alone keeps its own mask", _ {
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::None;
		r.base.greyout = {0xFFFFFFFEu, 0};
		expect(r.open()).to_be_true();

		uint32_t cols = 0;
		uint32_t rows = 0;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_true();
		expect(cols).to_equal(uint32_t(0xFFFFFFFE));
	});

	it("reports the greyout while it owns the pads, so legacy greys the guest's pixels once", _ {
		// Legacy greys whatever `PadLEDs::image` holds in its send path (`prepareColour`,
		// `hid/led/pad_leds.cpp:284-298`), the guest's pixels included.
		Rig r;
		r.base.overlay = true;
		r.base.regions = PadRegions::Main;
		r.base.greyout = {0b10u, 0};
		expect(r.open()).to_be_true();

		uint32_t cols = 0;
		uint32_t rows = 0;
		expect(r.guest.getGreyoutColsAndRows(&cols, &rows)).to_be_true();
		expect(cols).to_equal(uint32_t(0b10));
	});

	it("builds its shell with greys_in_send_path on", _ {
		// The guest's pads reach `PadLEDs::image` as the scene painted them, ungreyed, however the
		// stack asks to grey them: legacy's send path does that.
		Rig r;
		r.base.occludes = true;
		r.base.greyout = {0xFFFFFFFFu, 0xFFu};
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
				expect(PadLEDs::image[y][x]).to_equal(PatternScene::pad_for(x, y));
			}
		}
	});
});
// clang-format on

DELUGE_SPEC(shell_host_ui_drawing_spec, shell_host_ui_drawing)
