#include "../gfx/support/golden.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/font.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/core/state.h"
#include "ui/core/style.h"
#include "ui/oled/popup.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::Canvas;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

// `option_window` -- the pixels of legacy `ContextMenu::renderOLED`
// (`gui/context_menu/context_menu.cpp`) -- pinned the three ways the rest of this directory pins a
// widget.
//
// First, byte for byte against the legacy routine: `legacy::render_oled` below is that method
// transcribed against the raw gfx `Canvas` in canvas-absolute coordinates, with every magic number
// spelled as a literal (100/40 for the window, 22 for the highlight's left edge, 15/6/18 for the
// rule/heading/first-row offsets, 30/27/44 for the three `OLED_MAIN_WIDTH_PIXELS - n` insets, 6/9
// for the Apple ][ font) rather than borrowed from the constants the port lays out with. The one
// legacy call deliberately not transcribed is `OLED::setupSideScroller`, which arms *later*
// frames' scrolling and inks nothing on the frame it is called from -- the port's `scroll_text`
// draws at offset 0 on a fresh `ScrollText`, which is the same pixels.
//
// Second, as individual coordinates read off the legacy source: the window's border, the rule's
// row and extent, and where the highlight's edges land relative to the label's.
//
// Third, as golden PBMs at both shipped geometries.
//
// Points worth calling out explicitly:
//
//  - **The label box and the highlight box are different rectangles.** Legacy draws the label at
//    x 23 clipped to `width - 27` (101) and inverts from x 22 across `width - 44` (84) px, i.e.
//    columns 22..105. That is why this is a widget of its own rather than a `list` call: `list`
//    derives label origin, right clip and highlight extent from the one rect it is given, and
//    insets the label by `style.inset_x` (6) where legacy insets by 1.
//  - **The window is square-cornered.** Legacy's `drawRectangle` is the square primitive, so the
//    port uses `PopupStyle::Permanent`, not `Framed`.
//  - **Centred on the whole panel**, the five rows the faceplate hides included, exactly as
//    `popup_rect_for` is: `windowMinY = (OLED_MAIN_HEIGHT_PIXELS - 40) >> 1` is 4 on the 48-row
//    panel and 12 on the 64-row one. On the 48-row panel that puts the window's top border *under
//    the faceplate*, where legacy inks it into a framebuffer nobody sees and the port's root frame
//    clips it away; see `below_bezel` for why the 48-row comparisons skip those five rows and the
//    64-row ones (no hidden rows at all) compare the whole panel.
//  - **The focused row's label side-scrolls**, and that motion is only visible across *quiet
//    ticks* -- one frame cannot show it. The last group drives several ticks with no input at all.

namespace {

/// @brief `ContextMenu::renderOLED`, transcribed.
///
/// @param c       Canvas to draw into, in canvas-absolute coordinates.
/// @param panel_w `OLED_MAIN_WIDTH_PIXELS`.
/// @param panel_h `OLED_MAIN_HEIGHT_PIXELS`.
/// @param title   `getTitle()`.
/// @param options `getOptions()`.
/// @param current `currentOption`.
/// @param scroll  `scrollPos`.
/// @param invert  `FlashStorage::accessibilityMenuHighlighting != MenuHighlighting::NO_INVERSION`,
///                which is what `Canvas::menuHighlightingInverts` mirrors.
void legacy_render_oled(Canvas& c, int32_t panel_w, int32_t panel_h, std::string_view title,
                        std::span<const std::string_view> options, int32_t current, int32_t scroll, bool invert,
                        std::string_view footer = {}) {
	const int32_t windowWidth = 100;
	const int32_t windowHeight = 40;

	const int32_t windowMinX = (panel_w - windowWidth) >> 1;
	const int32_t windowMaxX = panel_w - windowMinX;
	const int32_t windowMinY = (panel_h - windowHeight) >> 1;
	const int32_t windowMaxY = panel_h - windowMinY;

	c.clear_area_exact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1);
	c.draw_rectangle(windowMinX, windowMinY, windowMaxX, windowMaxY);
	c.draw_horizontal_line(windowMinY + 15, 22, panel_w - 30);
	c.draw_string(title, 22, windowMinY + 6, 6, 9);

	int32_t textPixelY = windowMinY + 18;
	int32_t option = scroll;
	int32_t i = 0;
	while (option < static_cast<int32_t>(options.size()) && i < 2) {
		const int32_t invertStartX = 22;
		int32_t textPixelX = invertStartX + 1;
		if (!invert) {
			textPixelX += 6; // kTextSpacingX
		}
		c.draw_string(options[static_cast<size_t>(option)], textPixelX, textPixelY, 6, 9, 0, panel_w - 27);
		if (option == current) {
			// `invertLeftEdgeForMenuHighlighting(invertStartX, OLED_MAIN_WIDTH_PIXELS - 44,
			// textPixelY, textPixelY + 8)` (`oled_canvas/canvas.cpp:26-33`).
			if (invert) {
				c.invert_area_rounded(invertStartX, panel_w - 44, textPixelY, textPixelY + 8);
			}
			else {
				c.draw_vertical_line(invertStartX, textPixelY, textPixelY + 8);
			}
		}
		textPixelY += 9;
		i++;
		option++;
	}

	// `AudioInputSelector::renderOLED`'s tail (`gui/context_menu/audio_input_selector.cpp:233-236`),
	// which runs *after* the base renderer and is the only legacy subclass that draws past it. Note
	// the different right clip (`- 26`, not the rows' `- 27`) and the different left origin (22, the
	// heading's, not the rows' `invertStartX + 1`).
	if (!footer.empty()) {
		c.draw_string(footer, 22, windowMinY + 20 + 9, 6, 9, 0, panel_w - 26);
	}
}

/// @brief Drop the rows the faceplate hides from a PBM, so a port that clips them can be compared
///        against a legacy oracle that does not.
///
/// The window's top edge is at canvas row `(48 - 40) >> 1 == 4` on the shipped 48-row panel, i.e.
/// *inside* the five-row bezel: legacy inks it into a framebuffer nobody can see, and the port's
/// root frame (origin `(0, visible_top)`) clips it away instead. The same is true of every popup
/// this toolkit draws -- `RenameScene`'s 120x40 window has its top edge on the same row, and its
/// recorded goldens have that row blank. Not a divergence in what the user sees; the rows compared
/// below are exactly the ones that reach the glass.
///
/// @param pbm         A P1 PBM as `page_major_to_pbm` writes it.
/// @param visible_top How many leading pixel rows the faceplate hides.
/// @return The same PBM with those rows removed from the body (the header is kept as-is).
std::string below_bezel(const std::string& pbm, int visible_top) {
	size_t at = 0;
	for (int i = 0; i < 2 + visible_top; i++) {
		at = pbm.find('\n', at) + 1;
	}
	return pbm.substr(at);
}

constexpr std::string_view kAreYouSure = "Are you sure?";
constexpr std::array<std::string_view, 1> kOneOption{{"OK"}};
constexpr std::array<std::string_view, 3> kThreeOptions{{"NEW SYNTH", "NEW KIT", "NEW AUDIO TRACK"}};

std::span<const std::string_view> one() {
	return {kOneOption.data(), kOneOption.size()};
}
std::span<const std::string_view> three() {
	return {kThreeOptions.data(), kThreeOptions.size()};
}

} // namespace

// clang-format off
describe option_window_w("ui::oled::option_window", $ {

	it("draws legacy's window, rule and heading", _ {
		uitest::Rig48 r;
		ScrollText label;
		option_window(r.frame, kAreYouSure, one(), 0, 0, label);

		legacy_render_oled(r.reference, 128, 48, kAreYouSure, one(), 0, 0, /*invert=*/true);
		expect(below_bezel(r.pbm(), 5)).to_equal(below_bezel(r.reference_pbm(), 5));

		// windowMinX = (128 - 100) >> 1 = 14, windowMaxX = 114; windowMinY = (48 - 40) >> 1 = 4,
		// windowMaxY = 44. A SQUARE rectangle -- `drawRectangle`, not the rounded primitive -- so
		// all four corners are lit... except that the top two are on canvas row 4, under the
		// faceplate, and the port clips them where legacy inked them invisibly (see `below_bezel`).
		expect(r.lit(14, 4)).to_be_false();
		expect(r.lit(14, 5)).to_be_true();
		expect(r.lit(114, 5)).to_be_true();
		expect(r.lit(14, 44)).to_be_true();
		expect(r.lit(114, 44)).to_be_true();
		expect(r.lit(13, 5)).to_be_false();
		expect(r.lit(115, 44)).to_be_false();

		// `drawHorizontalLine(windowMinY + 15, 22, OLED_MAIN_WIDTH_PIXELS - 30)`: canvas row 19,
		// columns 22..98 inclusive, and nothing outside that span.
		expect(r.lit(22, 19)).to_be_true();
		expect(r.lit(98, 19)).to_be_true();
		expect(r.lit(21, 19)).to_be_false();
		expect(r.lit(99, 19)).to_be_false();

		// The one option is focused, so its row is inverted: `invertAreaRounded(22, 84, 22, 30)`
		// covers canvas columns 22..105 on rows 22..30, with the rounded corners left alone.
		expect(r.lit(22, 23)).to_be_true();
		expect(r.lit(105, 23)).to_be_true();
		expect(r.lit(106, 23)).to_be_false();
		expect(r.lit(21, 23)).to_be_false();
		// ...and the row below the second (undrawn) slot is untouched interior.
		expect(r.lit(22, 40)).to_be_false();
	});

	it("shows two options at a time, at whichever window the caller chose", _ {
		// Legacy's `while (... i >= 2) break;`: never more than two rows, starting at `scrollPos`.
		for (const auto [current, scroll] : std::array<std::pair<int32_t, int32_t>, 4>{
		         {{0, 0}, {1, 0}, {1, 1}, {2, 1}}}) {
			uitest::Rig48 r;
			ScrollText label;
			option_window(r.frame, kAreYouSure, three(), current, scroll, label);
			legacy_render_oled(r.reference, 128, 48, kAreYouSure, three(), current, scroll, true);
			expect(below_bezel(r.pbm(), 5)).to_equal(below_bezel(r.reference_pbm(), 5));
		}
	});

	it("clamps a focused row the window does not contain", _ {
		// Defensive, and not a legacy behaviour -- `selectEncoderAction` maintains the invariant --
		// but `setupAndCheckAvailability` and a restored state both write the pair directly, and a
		// window that did not contain the cursor would draw no highlight at all.
		uitest::Rig48 r;
		ScrollText label;
		option_window(r.frame, kAreYouSure, three(), /*selected=*/2, /*first=*/0, label);
		// Clamped to {selected - 1} = 1, i.e. the same frame as the legitimate {2, 1} pair.
		legacy_render_oled(r.reference, 128, 48, kAreYouSure, three(), 2, 1, true);
		expect(below_bezel(r.pbm(), 5)).to_equal(below_bezel(r.reference_pbm(), 5));
	});

	it("clears only its own window over whatever is beneath", _ {
		// The scene is `oledShowsUIUnderneath` (`ContextMenu`'s constructor), so this runs over a
		// canvas the view underneath has already painted. Legacy clears the window *interior*
		// (`clearAreaExact(minX + 1, ...)`) and then inks the border ring; the port clears the ring
		// too and then inks it -- the same pixels, which is what this pins.
		uitest::Rig48 r;
		for (int32_t y = 0; y < 48; y += 2) {
			r.canvas.draw_horizontal_line(y, 0, 127);
			r.reference.draw_horizontal_line(y, 0, 127);
		}
		ScrollText label;
		option_window(r.frame, kAreYouSure, one(), 0, 0, label);
		legacy_render_oled(r.reference, 128, 48, kAreYouSure, one(), 0, 0, true);
		expect(below_bezel(r.pbm(), 5)).to_equal(below_bezel(r.reference_pbm(), 5));
		// The stripes survive outside the window and are gone inside it.
		expect(r.lit(0, 10)).to_be_true();
		expect(r.lit(60, 10)).to_be_false();
	});

	it("draws the NO_INVERSION highlight as a left-edge rule, with the label a cell further right", _ {
		// `MenuHighlighting::NO_INVERSION` (`FlashStorage::accessibilityMenuHighlighting`), which
		// this layer reads as `style.invert_highlight` -- the same substitution `list`,
		// `columns` and `text_edit` make for the same legacy primitive.
		uitest::Rig48 r;
		Style plain = deluge::ui::kDelugeStyle48;
		plain.invert_highlight = false;
		OledFrame f = root_frame(r.canvas, plain, r.ctx);
		ScrollText label;
		option_window(f, kAreYouSure, three(), 1, 0, label);

		legacy_render_oled(r.reference, 128, 48, kAreYouSure, three(), 1, 0, /*invert=*/false);
		expect(below_bezel(r.pbm(), 5)).to_equal(below_bezel(r.reference_pbm(), 5));

		// The rule is a single column at x 22, rows 31..39 (the second row's band).
		expect(r.lit(22, 31)).to_be_true();
		expect(r.lit(22, 39)).to_be_true();
		expect(r.lit(23, 31)).to_be_false();
	});

	it("lays the whole window out against the taller panel too", _ {
		// windowMinY = (64 - 40) >> 1 = 12, so every offset below it moves with the window; the
		// three horizontal insets are panel-width-relative and do not.
		uitest::Rig64 r;
		ScrollText label;
		option_window(r.frame, kAreYouSure, three(), 1, 0, label);
		// The 64-row panel has no hidden rows at all (`visible_top == 0`), so this one is compared
		// whole -- the whole window, top border included.
		legacy_render_oled(r.reference, 128, 64, kAreYouSure, three(), 1, 0, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect(r.lit(14, 12)).to_be_true();
		expect(r.lit(114, 52)).to_be_true();
	});

	it("draws nothing but the empty window for an empty option list", _ {
		uitest::Rig48 r;
		ScrollText label;
		option_window(r.frame, kAreYouSure, {}, 0, 0, label);
		legacy_render_oled(r.reference, 128, 48, kAreYouSure, {}, 0, 0, true);
		expect(below_bezel(r.pbm(), 5)).to_equal(below_bezel(r.reference_pbm(), 5));
	});

	it("side-scrolls the focused label across quiet ticks", _ {
		// `OLED::setupSideScroller(0, options[currentOption], textPixelX, OLED_MAIN_WIDTH_PIXELS -
		// 27, ...)`: the focused row's label ping-pongs inside a 78px box when it does not fit.
		// A single frame cannot show this -- the first frame always draws at offset 0 -- so the
		// widget is driven across ticks with no input at all, which is the only thing that moves it.
		constexpr std::string_view kLong[] = {"A VERY LONG OPTION LABEL INDEED"};
		uitest::Rig48 r;
		ScrollText label;

		r.begin_tick(0);
		option_window(r.frame, kAreYouSure, {kLong, 1}, 0, 0, label);
		// 31 characters at 6px is 186px, well past the 78px box, so it is armed -- with the
		// kScrollTime (400ms) pause every newly-appeared label gets, not an immediate step.
		expect(static_cast<int>(label.text_w_last)).to_equal(186);
		expect(static_cast<int>(label.rect_w_last)).to_equal(78);
		expect(static_cast<int>(label.offset)).to_equal(0);
		expect(static_cast<int>(label.next_step_ms)).to_equal(kScrollTime);
		const std::string first_frame = r.pbm();

		// A quiet tick before the pause is up changes nothing at all.
		r.begin_tick(kScrollTime - 1);
		option_window(r.frame, kAreYouSure, {kLong, 1}, 0, 0, label);
		expect(static_cast<int>(label.offset)).to_equal(0);
		expect(r.pbm()).to_equal(first_frame);

		// The tick the pause expires on steps one pixel, and re-arms at the 15ms forward cadence.
		r.begin_tick(kScrollTime);
		option_window(r.frame, kAreYouSure, {kLong, 1}, 0, 0, label);
		expect(static_cast<int>(label.offset)).to_equal(1);
		expect(static_cast<int>(label.next_step_ms)).to_equal(kScrollTime + kScrollStepForwardMs);
		expect(r.pbm() == first_frame).to_be_false();

		// ...and it keeps going on further quiet ticks.
		uint32_t now = kScrollTime;
		for (int i = 0; i < 5; i++) {
			now += static_cast<uint32_t>(kScrollStepForwardMs);
			r.begin_tick(now);
			option_window(r.frame, kAreYouSure, {kLong, 1}, 0, 0, label);
		}
		expect(static_cast<int>(label.offset)).to_equal(6);

		// A label that fits never scrolls, however long it is left alone.
		uitest::Rig48 still;
		ScrollText fits;
		still.begin_tick(0);
		option_window(still.frame, kAreYouSure, one(), 0, 0, fits);
		const std::string fixed = still.pbm();
		still.begin_tick(10000);
		option_window(still.frame, kAreYouSure, one(), 0, 0, fits);
		expect(static_cast<int>(fits.offset)).to_equal(0);
		expect(still.pbm()).to_equal(fixed);
	});

	it("claims no input, so it can be drawn on an unfocused frame", _ {
		// Render-only, as `text_edit` is: the select encoder's turn and the accept press belong to
		// the scene (`ContextMenu::selectEncoderAction` / `buttonAction`).
		uitest::Rig48 r;
		r.input.turn = 2;
		r.input.select_pressed = true;
		ScrollText label;
		option_window(r.frame, kAreYouSure, three(), 1, 0, label);
		expect(r.ctx.claims.turn).to_be_false();
		expect(r.ctx.claims.select).to_be_false();
	});

	it("records both shipped geometries as goldens", _ {
		{
			uitest::Rig48 r;
			ScrollText label;
			option_window(r.frame, kAreYouSure, one(), 0, 0, label);
			expect_golden(self, "../ui/widgets/option_window_confirm_48.pbm", r.pbm());
		}
		{
			uitest::Rig64 r;
			ScrollText label;
			option_window(r.frame, kAreYouSure, one(), 0, 0, label);
			expect_golden(self, "../ui/widgets/option_window_confirm_64.pbm", r.pbm());
		}
		{
			// The scrolled window: three options, the window on {1, 2} with the cursor on the
			// bottom row -- the shape `ContextMenu::selectEncoderAction` produces walking down.
			uitest::Rig48 r;
			ScrollText label;
			option_window(r.frame, kAreYouSure, three(), 2, 1, label);
			expect_golden(self, "../ui/widgets/option_window_scrolled_48.pbm", r.pbm());
		}
		{
			uitest::Rig64 r;
			ScrollText label;
			option_window(r.frame, kAreYouSure, three(), 2, 1, label);
			expect_golden(self, "../ui/widgets/option_window_scrolled_64.pbm", r.pbm());
		}
	});

	it("matches legacy over the two-pixel overlap between the footer and the second option row", _ {
		// A two-row window, which is the case the shipped descriptor cannot produce -- it forces its
		// option onto row 0 whenever a footer is up -- so the footer's band and the second row's
		// really do overlap here. Both sides are the same two `draw_string`s over the same pixels.
		//
		// **This does not pin the draw order, and does not claim to.** The glyph blit is `|=`
		// (`gfx/oled/canvas.cpp:626`), so the union is the same whichever runs first: moving the
		// footer draw before the rows and re-running this example passes -- checked, in review round
		// 1. The widget draws it last anyway because legacy does (see popup.h). What this example
		// *does* pin is that the overlapping case renders identically to legacy at all.
		uitest::Rig48 r;
		ScrollText sc;
		const std::string_view footer = "TRACK 1";
		option_window(r.frame, kAreYouSure, three(), 0, 0, sc, /*allow_motion=*/true, footer);
		legacy_render_oled(r.reference, 128, 48, kAreYouSure, three(), 0, 0, /*invert=*/true, footer);
		expect(below_bezel(r.pbm(), 5)).to_equal(below_bezel(r.reference_pbm(), 5));
	});

	it("would notice a clear under the footer, which legacy does not do", _ {
		// The discriminator the overlap example is not. Clearing the footer's band before drawing it
		// would erase the bottom rows of the option row above; legacy leaves them alone. Asserted on
		// the shared rows rather than left as prose.
		uitest::Rig48 r;
		ScrollText sc;
		option_window(r.frame, kAreYouSure, three(), 0, 0, sc, /*allow_motion=*/true, "TRACK 1");
		// windowMinY = 4; the second option row's band starts at 4 + 18 + 9 = 31, the footer's at
		// 4 + 20 + 9 = 33, so rows 33..39 carry ink from both.
		bool second_row = false, shared = false;
		for (int x = 0; x < 128; x++) {
			second_row = second_row || r.lit(x, 32);
			shared = shared || r.lit(x, 34);
		}
		expect(second_row).to_be_true();
		expect(shared).to_be_true();
	});

	it("draws the footer where AudioInputSelector draws it, at both geometries", _ {
		// The one legacy subclass that draws past the base renderer. Its row is
		// `windowMinY + 20 + kTextSpacingY` -- **two pixels below** the second option row's
		// `windowMinY + 18 + 9`, and drawn without clearing first, so the two would collide if a
		// descriptor ever showed both. `AudioInputSelector` never does: it forces its own option onto
		// the first row whenever the footer is up. Asserted at both geometries because it is a row
		// constant.
		const std::string_view footer = "TRACK 1";
		{
			uitest::Rig48 r;
			ScrollText sc;
			option_window(r.frame, kAreYouSure, one(), 0, 0, sc, /*allow_motion=*/true, footer);
			legacy_render_oled(r.reference, 128, 48, kAreYouSure, one(), 0, 0, /*invert=*/true, footer);
			expect(below_bezel(r.pbm(), 5)).to_equal(below_bezel(r.reference_pbm(), 5));
			// windowMinY = (48 - 40) >> 1 = 4, so the footer's own row is 4 + 20 + 9 = 33 and it inks
			// from the heading's x (22) rather than the rows' 23.
			bool any = false;
			for (int x = 0; x < 128; x++) {
				any = any || r.lit(x, 33 + 2);
			}
			expect(any).to_be_true();
		}
		{
			uitest::Rig64 r;
			ScrollText sc;
			option_window(r.frame, kAreYouSure, one(), 0, 0, sc, /*allow_motion=*/true, footer);
			legacy_render_oled(r.reference, 128, 64, kAreYouSure, one(), 0, 0, /*invert=*/true, footer);
			expect(r.pbm()).to_equal(r.reference_pbm()); // no bezel on the 64-row panel
		}
	});

	it("draws nothing extra when the footer is empty, which is every other descriptor", _ {
		uitest::Rig48 plain;
		ScrollText sc1;
		option_window(plain.frame, kAreYouSure, one(), 0, 0, sc1);

		uitest::Rig48 explicit_empty;
		ScrollText sc2;
		option_window(explicit_empty.frame, kAreYouSure, one(), 0, 0, sc2, /*allow_motion=*/true, {});

		expect(explicit_empty.pbm()).to_equal(plain.pbm());
	});
});
// clang-format on
DELUGE_SPEC(ui_option_window_spec, option_window_w)
