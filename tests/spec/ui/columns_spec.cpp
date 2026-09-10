#include "../gfx/support/golden.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/icons.h"
#include "support/frames.h"
#include "ui/core/style.h"
#include "ui/oled/columns.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::BorderRadius;
using deluge::gfx::oled::Canvas;
using deluge::gfx::oled::Rect;
namespace icons = deluge::gfx::oled::icons;
namespace uitest = deluge::ui::test;

// The horizontal menu page is pinned three ways at once, as `meters_spec.cpp` and
// `values_spec.cpp` pin their widgets.
//
// First, byte for byte against the legacy renderer itself: `namespace legacy` below
// re-transcribes `HorizontalMenu::renderMenuItems`, `renderColumnLabel`, `renderPageCounters` and
// `renderTitle` together with the four slot renderings a column can have --
// `Submenu::renderInHorizontalMenu`, `Toggle::renderInHorizontalMenu`,
// `Enumeration::renderInHorizontalMenu` and `Number::drawKnob` / `drawBar` (which
// `Decimal::renderInHorizontalMenu` reaches through `Number` for a non-NUMBER style) -- against
// the raw gfx `Canvas` in canvas-absolute coordinates, with every magic number spelled as a
// literal (14 for base_y, 25 for the box, 6/9 for the Apple ][ font, 9/10 for the title font, 7
// for the arrow) rather than borrowed from the constants the port lays out with. Every scene is
// drawn both ways and the whole canvas compared.
//
// Second, as individual coordinates read off the legacy source, for a handful of checks worth
// pinning explicitly: the label row, the focus inversion, the turn, and the "1/2" counters with
// their slanted separator.
//
// Third, as golden PBMs, at both shipped geometries.
//
// Points where the port's interface or behaviour needed a decision, all resolved in the legacy
// code's favour except where legacy has no behaviour to favour:
//
//  - `columns` takes a rect (`columns(f, rect, ColumnsState&, span<Column>)`), plus a
//    `title_text`. **`f.bounds()` is the rect that reproduces legacy** -- not `menu_rect(f)`,
//    which starts below the title band this widget draws into -- and every golden is recorded
//    with it.
//  - `base_y` is `kHorizontalMenuBaseY` (14) on *both* panels, because legacy's is a `constexpr`
//    literal `14 + OLED_MAIN_TOPMOST_PIXEL` with no panel-height branch. It is NOT list's
//    `kMenuBaseY48/64`, which is `MenuItem::drawItemsForOled`'s
//    `(OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14` and belongs to a different renderer. The two
//    coincide at 48 and differ by a row at 64, so the oracle below spells legacy's literal
//    rather than the port's constant -- encoding the port's rule there would make the 64-row
//    comparison circular.
//  - The non-inverting highlight rule is at `rect.bottom() - 3`. Legacy draws it at
//    `OLED_MAIN_VISIBLE_HEIGHT + 2`, a canvas-absolute row that is the same pixel on the shipped
//    panel but falls off the bottom of a panel with no bezel. Same reading for the placeholder
//    frames' `OLED_MAIN_HEIGHT_PIXELS - 7`.
//  - The value region handed to `Column::render` is a frame whose clip is one row taller than
//    the region at the top, because `drawKnob` and `Toggle::renderInHorizontalMenu` blit 16-row
//    icons at `start_y - 1` and `OledFrame`'s icon primitive is all-or-nothing against its clip.
//    A tight region would draw no knob arc and no toggle switch at all; the scenes below fail if
//    it is tightened.
//  - There is no legacy behaviour for `turn`: the horizontal menu moved focus with the instrument
//    buttons and spent the select encoder on the focused value. The walk is `list`'s -- one
//    relevant column per detent, stopping at the ends.

namespace {

/// Verbatim transcriptions of the legacy renderers, drawing through the raw gfx Canvas in
/// canvas-absolute coordinates. `image.` and the parameter names are legacy's own.
namespace legacy {

/// What `renderMenuItems` asks of each item it draws: `getColumnLabel`, `getOccupiedSlots`,
/// `showColumnLabel`, `isItemRelevant`, `isSubmenu` and `renderInHorizontalMenu`.
struct Item {
	enum Kind : uint8_t { KNOB, BAR, ENUM, SUBMENU, TOGGLE };
	const char* label = "";
	Kind kind = KNOB;
	float norm = 0.0f;
	const char* text = "";
	bool on = false;
	uint8_t slots = 1;
	bool show_label = true;
	bool relevant = true;
};

bool isSubmenu(const Item& item) {
	return item.kind == Item::SUBMENU;
}

/// `Number::drawKnob`.
void drawKnob(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t slot_w, float norm) {
	image.draw_icon_centered(icons::knobArc, slot_x, slot_w, slot_y - 1);

	constexpr uint8_t knob_radius = 10;
	const uint8_t center_x = slot_x + slot_w / 2;
	const uint8_t center_y = slot_y + knob_radius;
	constexpr uint8_t arc_range_angle = 210;
	constexpr uint8_t beginning_angle = 165;
	const float current_angle = beginning_angle + (arc_range_angle * norm);

	const float radians = current_angle * M_PI / 180.0f;
	constexpr float outer_radius = knob_radius - 1.0f;
	constexpr float inner_radius = 3.5f;
	const float cos_a = std::cos(radians);
	const float sin_a = std::sin(radians);
	image.draw_line(std::round(center_x + inner_radius * cos_a), std::round(center_y + inner_radius * sin_a),
	                std::round(center_x + outer_radius * cos_a), std::round(center_y + outer_radius * sin_a),
	                {.thick = true});
}

/// `Number::drawBar`.
void drawBar(Canvas& image, int32_t slot_x, int32_t slot_y, float norm) {
	constexpr uint8_t bar_width = 21;
	constexpr uint8_t bar_height = 5;
	constexpr uint8_t outline_padding = 2;
	const uint8_t bar_start_x = slot_x + 5;
	const uint8_t bar_start_y = slot_y + 2 + 2;
	const uint8_t bar_end_x = bar_start_x + bar_width - 1;
	const uint8_t bar_end_y = bar_start_y + bar_height - 1;

	image.draw_rectangle(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                     bar_end_y + outline_padding);
	image.invert_area(bar_start_x, static_cast<uint8_t>(norm * bar_width), bar_start_y, bar_end_y);
}

/// `Enumeration::renderInHorizontalMenu`. `Decimal`'s NUMBER-style arm is the same call with a
/// formatted value.
void drawEnum(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t slot_w, const char* short_option) {
	image.draw_string_centered(short_option, slot_x, slot_y + 2, 6, 9, slot_w);
}

/// `OLED::submenuArrowIconBold`, byte for byte, so the icon added to the gfx registry is checked
/// rather than assumed.
const uint8_t submenuArrowIconBold[] = {
    0b00000000, //<
    0b00000000, //<
    0b11000110, //<
    0b01101100, //<
    0b00111000, //<
    0b00010000, //<
    0b00000000, //<
};

/// `Submenu::renderInHorizontalMenu`.
void drawSubmenuArrow(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t slot_w) {
	const int32_t arrow_y = slot_y + 2;
	const int32_t arrow_x = slot_x + (slot_w - 7) / 2 - 1;
	image.draw_graphic_multi_line(submenuArrowIconBold, arrow_x, arrow_y, 7);
}

/// `Toggle::renderInHorizontalMenu`.
void drawToggle(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t slot_w, bool on) {
	image.draw_icon_centered(on ? icons::switcherOn : icons::switcherOff, slot_x, slot_w, slot_y - 1);
}

void renderInHorizontalMenu(Canvas& image, const Item& item, int32_t start_x, int32_t start_y, int32_t width) {
	switch (item.kind) {
	case Item::KNOB:
		return drawKnob(image, start_x, start_y, width, item.norm);
	case Item::BAR:
		return drawBar(image, start_x, start_y, item.norm);
	case Item::ENUM:
		return drawEnum(image, start_x, start_y, width, item.text);
	case Item::SUBMENU:
		return drawSubmenuArrow(image, start_x, start_y, width);
	case Item::TOGGLE:
		return drawToggle(image, start_x, start_y, width, item.on);
	}
}

/// `HorizontalMenu::renderColumnLabel`, with `deluge::string::removeSpaces` inlined.
void renderColumnLabel(Canvas& image, const Item& item, int32_t labelY, int32_t slotStartX, int32_t slotWidth,
                       bool isSelected) {
	std::string label;
	for (const char* p = item.label; *p != '\0'; p++) {
		if (std::isspace(static_cast<unsigned char>(*p)) == 0) {
			label += *p;
		}
	}

	int32_t label_width = 0;
	while ((label_width = image.get_string_width_in_pixels(label.c_str(), 9)) + 4 >= slotWidth) {
		if (label.empty()) {
			break; // the legacy resize() would have underflowed
		}
		label.resize(label.size() - 1);
	}

	const int32_t label_start_x = slotStartX + (slotWidth - label_width) / 2;
	image.draw_string(label, label_start_x, labelY, 6, 9);

	if (item.slots > 1 && !isSubmenu(item) && !isSelected) {
		const int32_t y = labelY + 4;
		image.draw_horizontal_line(y, slotStartX + 4, label_start_x - 4);
		image.draw_horizontal_line(y, label_start_x + label_width + 2, slotStartX + slotWidth - 6);
	}
}

/// `HorizontalMenu::renderPageCounters`. `etl::string<2>` truncates, hence the resize.
void renderPageCounters(Canvas& image, int32_t visiblePageNumber, int32_t totalPages, int32_t width, int32_t topmost) {
	if (totalPages <= 1) {
		return;
	}
	const int32_t y = 1 + topmost;
	int32_t x = width - 6 - 1;

	std::string currentPageNum = std::to_string(totalPages);
	if (currentPageNum.size() > 2) {
		currentPageNum.resize(2);
	}
	image.draw_string(currentPageNum, x, y, 6, 9);
	x -= 6 - 1;

	image.draw_line(x, y + 9 - 2, x + 2, y + 1);
	x -= 6;

	currentPageNum = std::to_string(visiblePageNumber + 1);
	if (currentPageNum.size() > 2) {
		currentPageNum.resize(2);
	}
	image.draw_string(currentPageNum, x, y, 6, 9);
}

/// `HorizontalMenu::renderTitle`.
void renderTitle(Canvas& image, std::string title, int32_t totalPages, int32_t width) {
	if (totalPages > 1) {
		const uint8_t title_width = image.get_string_width_in_pixels(title.c_str(), 10);
		constexpr uint8_t paging_width = 6 * 2 + 10;

		if (title_width > width - paging_width) {
			const std::string title_copy(title);

			std::vector<std::string> words;
			size_t start = 0;
			while (start < title_copy.size()) {
				while (start < title_copy.size() && title_copy[start] == ' ') {
					start++;
				}
				if (start >= title_copy.size()) {
					break;
				}
				size_t end = start;
				while (end < title_copy.size() && title_copy[end] != ' ') {
					end++;
				}
				words.emplace_back(title_copy.substr(start, end - start));
				start = end;
			}

			if (words.size() == 2) {
				title = std::string(1, words[0][0]) + ". " + words[1];
			}
		}
	}

	image.draw_screen_title(title, false);
}

/// `HorizontalMenu::renderMenuItems` over one page's items, minus the container map the port
/// replaces with `Column::slots`. `items` is legacy's `paging.visiblePageItems` and `current`
/// the index of the current item within it.
/// `full_inversion` is `FlashStorage::accessibilityMenuHighlighting == FULL_INVERSION` and
/// `rounded` is the canvas's `roundedCornersEnabled` -- `style.invert_highlight` and
/// `style.rounded` in the port.
void renderMenuItems(Canvas& image, std::span<const Item> items, int32_t current, int32_t width, int32_t height,
                     int32_t topmost, bool full_inversion, bool rounded) {
	const int32_t base_y = 14 + topmost; // a literal on every panel, not list's kMenuBaseY48/64
	const int32_t column_width = width / 4;
	int32_t current_x = 0;

	for (int32_t i = 0; i < static_cast<int32_t>(items.size()); i++) {
		const Item& item = items[i];
		const bool is_selected = i == current;

		const int32_t box_width = column_width * item.slots;
		constexpr int32_t box_height = 25;
		constexpr int32_t label_height = 9;
		const int32_t label_y = base_y + box_height - label_height;
		int32_t content_height = box_height;

		if (item.show_label) {
			renderColumnLabel(image, item, label_y, current_x, box_width, is_selected);
			content_height -= label_height;
		}

		if (!item.relevant) {
			image.draw_string_centered("-", current_x, base_y + 2, 9, 10, box_width);
		}
		else {
			renderInHorizontalMenu(image, item, current_x, base_y, box_width - 1);
		}

		if (is_selected && (items.size() > 1 || items[0].slots < 4)) {
			if (full_inversion) {
				const bool highlight_whole_slot = !item.show_label || isSubmenu(item);
				const int32_t start_y = highlight_whole_slot ? base_y - 1 : label_y;
				const int32_t end_y = highlight_whole_slot ? base_y + box_height - 1 : label_y + label_height - 1;
				image.invert_area_rounded(current_x + 1, box_width - 3, start_y, end_y, BorderRadius::Small, rounded);
			}
			else {
				image.draw_horizontal_line(height - 3, current_x,
				                           current_x + box_width - 2); // canvas-absolute in legacy
			}
		}

		current_x += box_width;
	}

	for (int32_t n = current_x / (width / 4); n < 4; n++) {
		const int32_t start_x = n * column_width + 7;
		const int32_t end_x = start_x + 17;
		const int32_t start_y = base_y + 1;
		const int32_t end_y = height - 7; // canvas-absolute in legacy
		constexpr int32_t dot_interval = 5;

		for (int32_t x = start_x + 1; x < end_x; x += dot_interval) {
			image.draw_pixel(x, start_y);
			image.draw_pixel(x, end_y);
		}
		for (int32_t y = start_y + 3; y < end_y; y += dot_interval) {
			image.draw_pixel(start_x - 2, y);
			image.draw_pixel(end_x + 2, y);
		}
	}
}

/// `HorizontalMenu::renderOLED`, the three drawing calls of it.
void renderOLED(Canvas& image, const char* title, std::span<const Item> page, int32_t current, int32_t visible_page,
                int32_t total_pages, int32_t width, int32_t height, int32_t topmost, bool full_inversion,
                bool rounded) {
	if (title != nullptr) {
		renderTitle(image, title, total_pages, width);
	}
	renderPageCounters(image, visible_page, total_pages, width, topmost);
	renderMenuItems(image, page, current, width, height, topmost, full_inversion, rounded);
}

} // namespace legacy

/// The port's side of a scene: `Column`s over the same items, with the ready-made renderers and
/// stable storage for their contexts.
struct Page {
	std::vector<legacy::Item> items;
	std::vector<MeterColumn> meters;
	std::vector<TextColumn> texts;
	std::vector<ToggleColumn> toggles;
	std::vector<Column> cols;

	explicit Page(std::vector<legacy::Item> in) : items(std::move(in)) {
		meters.resize(items.size());
		texts.resize(items.size());
		toggles.resize(items.size());
		cols.resize(items.size());
		for (size_t i = 0; i < items.size(); i++) {
			const legacy::Item& item = items[i];
			Column& c = cols[i];
			c.label = item.label;
			c.slots = item.slots;
			c.show_label = item.show_label;
			c.relevant = item.relevant;
			c.is_submenu = item.kind == legacy::Item::SUBMENU;
			switch (item.kind) {
			case legacy::Item::KNOB:
				meters[i] = MeterColumn{.hint = Hint::Knob, .norm = item.norm};
				c.render = meter_column;
				c.ctx = &meters[i];
				break;
			case legacy::Item::BAR:
				meters[i] = MeterColumn{.hint = Hint::Bar, .norm = item.norm};
				c.render = meter_column;
				c.ctx = &meters[i];
				break;
			case legacy::Item::ENUM:
				texts[i] = TextColumn{.value = item.text};
				c.render = text_column;
				c.ctx = &texts[i];
				break;
			case legacy::Item::SUBMENU:
				c.render = submenu_column;
				break;
			case legacy::Item::TOGGLE:
				toggles[i] = ToggleColumn{.on = item.on};
				c.render = toggle_column;
				c.ctx = &toggles[i];
				break;
			}
		}
	}

	std::span<const Column> span() const { return {cols.data(), cols.size()}; }
	std::span<const legacy::Item> page(size_t first, size_t last) const {
		return std::span<const legacy::Item>{items.data() + first, last - first};
	}
};

/// A representative scene: FREQ (knob at 0.5), RES (bar at 0.25), MODE (the enum text "12dB")
/// and MORPH (a submenu arrow), focused on FREQ.
std::vector<legacy::Item> four_columns() {
	return {
	    legacy::Item{.label = "FREQ", .kind = legacy::Item::KNOB, .norm = 0.5f},
	    legacy::Item{.label = "RES", .kind = legacy::Item::BAR, .norm = 0.25f},
	    legacy::Item{.label = "MODE", .kind = legacy::Item::ENUM, .text = "12dB"},
	    legacy::Item{.label = "MORPH", .kind = legacy::Item::SUBMENU},
	};
}

/// Draw one scene both ways into \p r and compare the whole canvas.
template <class Self, class Rig>
void matches_legacy(Self& self, Rig& r, Page& p, ColumnsState& state, size_t first, size_t last, int32_t total_pages,
                    const char* title) {
	columns(r.frame, r.frame.bounds(), state, p.span(), title == nullptr ? std::string_view{} : title);
	legacy::renderOLED(r.reference, title, p.page(first, last), state.focused - static_cast<int32_t>(first), state.page,
	                   total_pages, r.canvas.geometry().width, r.canvas.geometry().height, r.frame.origin.y,
	                   r.style.invert_highlight, r.style.rounded);
	expect(r.pbm()).to_equal(r.reference_pbm());
}

} // namespace

// clang-format off
describe columns_w("ui::oled::columns", $ {
	it("draws the legacy four-column page at both geometries", _ {
		uitest::Rig48 r;
		Page p{four_columns()};
		ColumnsState state{};
		matches_legacy(self, r, p, state, 0, 4, 1, "FILTER");
		expect_golden(self, "../ui/widgets/columns_four_48.pbm", r.pbm());

		uitest::Rig64 r64;
		Page p64{four_columns()};
		ColumnsState state64{};
		matches_legacy(self, r64, p64, state64, 0, 4, 1, "FILTER");
		expect_golden(self, "../ui/widgets/columns_four_64.pbm", r64.pbm());
	});

	it("puts the label row at the legacy base_y + box_height - label_height", _ {
		// base_y 14 (+ the panel's 5 hidden rows), a 25px box, a 9px label at its bottom: canvas
		// rows 35..43, i.e. 5 + 14 + 25 - 9. The focused column's label box is inverted, so its
		// background pixels are the ones that light up, and they stop exactly at the row
		// boundaries.
		uitest::Rig48 r;
		Page p{four_columns()};
		ColumnsState state{};
		columns(r.frame, r.frame.bounds(), state, p.span());

		expect(r.lit(2, 34)).to_be_false(); // one row above the label box
		expect(r.lit(2, 35)).to_be_true();  // first row of it
		expect(r.lit(2, 43)).to_be_true();  // last row
		expect(r.lit(2, 44)).to_be_false(); // one row below

		// The inversion is `invertAreaRounded(current_x + 1, box_width - 3, ...)`: it starts one
		// pixel in from the column's left edge and is three pixels narrower than the box, with
		// the corners rounded back off (style.rounded).
		expect(r.lit(0, 39)).to_be_false();  // outside the inverted box
		expect(r.lit(1, 39)).to_be_true();   // its left edge, mid-height
		expect(r.lit(29, 39)).to_be_true();  // its right edge: 1 + (32 - 3) - 1
		expect(r.lit(30, 39)).to_be_false();
		expect(r.lit(1, 35)).to_be_false();  // rounded corner, cleared back off

		// An unfocused column's label is plain text on a dark background.
		expect(r.lit(34, 39)).to_be_false();

		// And on a 64-row panel with no bezel, the same literal base_y puts the label row at
		// 14 + 25 - 9 = 30..38 -- one row *above* where list's kMenuBaseY64 would have put it.
		uitest::Rig64 r64;
		Page p64{four_columns()};
		ColumnsState s64{};
		columns(r64.frame, r64.frame.bounds(), s64, p64.span());
		expect(r64.lit(2, 29)).to_be_false();
		expect(r64.lit(2, 30)).to_be_true();
		expect(r64.lit(2, 38)).to_be_true();
		expect(r64.lit(2, 39)).to_be_false();
	});

	it("does not move focus on a turn", _ {
		// `HorizontalMenu::selectEncoderAction` (`horizontal_menu.cpp:309-326`) edits the focused
		// column's value and never moves focus; `columns` no longer walks focus on the turn at
		// all -- the scene owns the encoder now (`MenuScene::render_horizontal`).
		uitest::Rig48 r;
		Page p{four_columns()};
		ColumnsState state{};

		r.input.turn = 1;
		const Response resp = columns(r.frame, r.frame.bounds(), state, p.span());
		expect(resp.changed).to_be_false();
		expect(static_cast<int32_t>(state.focused)).to_equal(0);

		// The highlight stays on the first column.
		expect(r.lit(2, 39)).to_be_true();
		expect(r.lit(34, 39)).to_be_false();

		// A negative turn does nothing either.
		uitest::Rig48 r2;
		Page p2{four_columns()};
		ColumnsState at_start{.focused = 2};
		r2.input.turn = -1;
		const Response stuck = columns(r2.frame, r2.frame.bounds(), at_start, p2.span());
		expect(stuck.changed).to_be_false();
		expect(static_cast<int32_t>(at_start.focused)).to_equal(2);
	});

	it("draws an irrelevant column as the legacy dash", _ {
		// The FIXED layout's disabled slot: a dash in the title face, centred across the whole
		// box.
		std::vector<legacy::Item> with_gap = four_columns();
		with_gap[1].relevant = false;
		uitest::Rig48 r;
		Page p{with_gap};
		ColumnsState state{};
		matches_legacy(self, r, p, state, 0, 4, 1, nullptr);
	});

	it("pages six columns and shows the legacy page counters", _ {
		std::vector<legacy::Item> six = four_columns();
		six.push_back(legacy::Item{.label = "DRIVE", .kind = legacy::Item::BAR, .norm = 0.75f});
		six.push_back(legacy::Item{.label = "MIX", .kind = legacy::Item::TOGGLE, .on = true});

		// Four one-slot columns fill a page, so this is two pages.
		Page p{six};
		expect(static_cast<int32_t>(paging_of(p.span(), 0).total_pages)).to_equal(2);
		expect(static_cast<int32_t>(paging_of(p.span(), 0).page)).to_equal(0);
		expect(static_cast<int32_t>(paging_of(p.span(), 4).page)).to_equal(1);
		expect(static_cast<int32_t>(paging_of(p.span(), 4).focused_on_page)).to_equal(0);
		expect(static_cast<int32_t>(paging_of(p.span(), 5).focused_on_page)).to_equal(1);

		uitest::Rig48 r;
		ColumnsState state{};
		matches_legacy(self, r, p, state, 0, 4, 2, "FILTER");
		expect_golden(self, "../ui/widgets/columns_paged_48.pbm", r.pbm());

		// renderPageCounters: "1/2" in the top right, the digits at canvas rows 6..14 and the
		// separator a slanted two-pixel line from (116, 13) to (118, 7).
		bool any_in_band = false;
		for (int32_t x = 110; x < 128; x++) {
			for (int32_t y = 6; y < 15; y++) {
				any_in_band = any_in_band || r.lit(x, y);
			}
		}
		expect(any_in_band).to_be_true();
		expect(r.lit(116, 13)).to_be_true(); // the separator's bottom end, y + kTextSpacingY - 2
		expect(r.lit(118, 7)).to_be_true();  // its top end, y + 1
		expect(r.lit(117, 10)).to_be_true(); // and the middle of the run between them

		// A single page draws no counters at all.
		uitest::Rig48 unpaged;
		Page p4{four_columns()};
		ColumnsState s4{};
		columns(unpaged.frame, unpaged.frame.bounds(), s4, p4.span(), "FILTER");
		bool anything = false;
		for (int32_t x = 100; x < 128; x++) {
			for (int32_t y = 6; y < 15; y++) {
				anything = anything || unpaged.lit(x, y);
			}
		}
		expect(anything).to_be_false();
	});

	it("finds the column under an instrument button on the focused page", _ {
		// `HorizontalMenu::handleInstrumentButtonPress`: walk the visible page's items,
		// accumulating occupied slots, and take the one whose [start, start + slots) covers the
		// button.
		std::vector<legacy::Item> items = {
		    legacy::Item{.label = "A", .kind = legacy::Item::KNOB},
		    legacy::Item{.label = "B", .kind = legacy::Item::KNOB, .slots = 2},
		    legacy::Item{.label = "C", .kind = legacy::Item::KNOB},
		};
		Page p{items};
		expect(column_under_button(p.span(), 0, 0)).to_equal(0);
		expect(column_under_button(p.span(), 0, 1)).to_equal(1);
		expect(column_under_button(p.span(), 0, 2)).to_equal(1);
		expect(column_under_button(p.span(), 0, 3)).to_equal(2);
	});

	it("finds nothing under a button past the page's slots", _ {
		std::vector<legacy::Item> items = {
		    legacy::Item{.label = "A", .kind = legacy::Item::KNOB},
		    legacy::Item{.label = "B", .kind = legacy::Item::KNOB},
		    legacy::Item{.label = "C", .kind = legacy::Item::KNOB},
		};
		Page p{items};
		expect(column_under_button(p.span(), 0, 3)).to_equal(-1);
	});

	it("switches page with wrap, keeping the position on the page", _ {
		// `switchVisiblePage` (`horizontal_menu.cpp:345-366`) wraps; `selectMenuItem` keeps
		// `selectedItemPositionOnPage`, falling back to the closest relevant item.
		std::vector<legacy::Item> eight;
		for (int i = 0; i < 8; i++) {
			eight.push_back(legacy::Item{.label = "X", .kind = legacy::Item::KNOB});
		}
		Page p{eight};
		// Two pages of four; focused = 2 is position 2 on page 0.
		expect(page_switch(p.span(), 2, 1)).to_equal(6);   // position 2 on page 1
		expect(page_switch(p.span(), 6, 1)).to_equal(2);   // wraps back to page 0, position 2
		expect(page_switch(p.span(), 1, -1)).to_equal(5);  // wraps to page 1, position 1
	});

	it("does not switch when there is one page", _ {
		Page p{four_columns()};
		expect(page_switch(p.span(), 1, 1)).to_equal(1);
		expect(page_switch(p.span(), 1, -1)).to_equal(1);
	});

	it("highlights the whole slot for a submenu or an unlabelled column", _ {
		// A submenu column (or one with no label) inverts the whole 25px box from base_y - 1,
		// not just the label row.
		uitest::Rig48 r;
		Page p{four_columns()};
		ColumnsState state{.focused = 3};
		matches_legacy(self, r, p, state, 0, 4, 1, nullptr);
		// The box is `invertAreaRounded(current_x + 1, box_width - 3, base_y - 1, base_y + 24)`,
		// i.e. canvas x 97..125 and rows 18..43, with the four corner pixels cleared back off by
		// the SMALL radius.
		expect(r.lit(98, 18)).to_be_true();  // canvas base_y - 1 == 5 + 14 - 1, its first row
		expect(r.lit(98, 43)).to_be_true();  // its last row, base_y + 25 - 1
		expect(r.lit(98, 44)).to_be_false();
		expect(r.lit(97, 30)).to_be_true();  // left edge, mid-height
		expect(r.lit(96, 30)).to_be_false(); // outside it
		expect(r.lit(97, 18)).to_be_false(); // the rounded corner

		std::vector<legacy::Item> bare = four_columns();
		bare[0].show_label = false;
		uitest::Rig48 r2;
		Page p2{bare};
		ColumnsState s2{};
		matches_legacy(self, r2, p2, s2, 0, 4, 1, nullptr);
	});

	it("draws the underline highlight when invert_highlight is off", _ {
		// The PARTIAL_INVERSION / NO_INVERSION arm.
		uitest::Rig48 r;
		Style plain = kDelugeStyle48;
		plain.invert_highlight = false;
		OledFrame f = root_frame(r.canvas, plain, r.ctx);
		Page p{four_columns()};
		ColumnsState state{};
		columns(f, f.bounds(), state, p.span());
		legacy::renderMenuItems(r.reference, p.page(0, 4), 0, 128, 48, f.origin.y, false, plain.rounded);
		expect(r.pbm()).to_equal(r.reference_pbm());

		// The rule is at canvas row 45 (OLED_MAIN_VISIBLE_HEIGHT + 2) and spans the box less
		// one pixel, and nothing is inverted.
		expect(r.lit(0, 45)).to_be_true();
		expect(r.lit(30, 45)).to_be_true();
		expect(r.lit(31, 45)).to_be_false();
		expect(r.lit(2, 39)).to_be_false();
	});

	it("gives a multi-slot column the legacy side ticks", _ {
		// A wide, unfocused, non-submenu column gets a rule either side of its label. The
		// focused one does not.
		std::vector<legacy::Item> wide = {
		    legacy::Item{.label = "OSC", .kind = legacy::Item::KNOB, .norm = 0.5f},
		    legacy::Item{.label = "WAVEFORM", .kind = legacy::Item::ENUM, .text = "SQUARE", .slots = 2},
		    legacy::Item{.label = "AMT", .kind = legacy::Item::BAR, .norm = 0.6f},
		};
		uitest::Rig48 r;
		Page p{wide};
		ColumnsState state{};
		matches_legacy(self, r, p, state, 0, 3, 1, nullptr);
		expect_golden(self, "../ui/widgets/columns_wide_48.pbm", r.pbm());

		// And with the wide column focused, where the ticks are suppressed.
		uitest::Rig48 r2;
		Page p2{wide};
		ColumnsState s2{.focused = 1};
		matches_legacy(self, r2, p2, s2, 0, 3, 1, nullptr);
	});

	it("keeps the knob arc and the toggle switch, which draw one row above the value box", _ {
		// Legacy's drawKnob and Toggle::renderInHorizontalMenu both blit a 16-row icon at
		// `slot.start_y - 1`, which is one row above the region `Column::render` is given.
		// OledFrame's icon primitive is all-or-nothing against the clip, so a region clipped
		// tight to the value box would draw neither icon at all.
		std::vector<legacy::Item> icons_only = {
		    legacy::Item{.label = "FREQ", .kind = legacy::Item::KNOB, .norm = 0.5f},
		    legacy::Item{.label = "MIX", .kind = legacy::Item::TOGGLE, .on = true},
		    legacy::Item{.label = "OFF", .kind = legacy::Item::TOGGLE, .on = false},
		};
		uitest::Rig48 r;
		Page p{icons_only};
		ColumnsState state{};
		matches_legacy(self, r, p, state, 0, 3, 1, nullptr);

		// The knob arc is 21 columns centred across the 31px region, so it starts at canvas
		// x 5, and its first column inks the bitmap's rows 8..13 -- canvas rows 26..31 with the
		// bitmap's top row at base_y - 1 == 18. The radial line cannot reach x 5 (it runs at
		// most 9px from a centre at x 15), so these pixels are the arc and nothing else.
		expect(r.lit(5, 25)).to_be_false();
		expect(r.lit(5, 26)).to_be_true();
		expect(r.lit(5, 31)).to_be_true();
		expect(r.lit(5, 32)).to_be_false();

		// A toggle column draws its switch icon and nothing else, so a value region clipped
		// tight to the box would leave the whole column blank.
		const auto ink_in = [&](int32_t x0, int32_t x1) {
			for (int32_t x = x0; x < x1; x++) {
				for (int32_t y = 19; y < 35; y++) { // the value box: base_y .. base_y + 15
					if (r.lit(x, y)) {
						return true;
					}
				}
			}
			return false;
		};
		expect(ink_in(32, 63)).to_be_true(); // the "on" switch
		expect(ink_in(64, 95)).to_be_true(); // the "off" switch
	});

	it("reports entered for a select press on a submenu column only", _ {
		uitest::Rig48 r;
		Page p{four_columns()};
		ColumnsState on_submenu{.focused = 3};
		r.input.select_pressed = true;
		expect(columns(r.frame, r.frame.bounds(), on_submenu, p.span()).entered).to_be_true();

		uitest::Rig48 r2;
		Page p2{four_columns()};
		ColumnsState on_value{.focused = 0};
		r2.input.select_pressed = true;
		expect(columns(r2.frame, r2.frame.bounds(), on_value, p2.span()).entered).to_be_false();
	});

	it("shortens a two-word title only when the page counters need the room", _ {
		// "MODULATION MATRIX" is wider than 128 - (6 * 2 + 10), so a paged menu draws it as
		// "M. MATRIX"; unpaged, it is drawn whole. A three-word title is never shortened.
		const auto both_ways = [&](const char* title, bool paged) {
			uitest::Rig48 r;
			title_paged(r.frame, title, paged);
			legacy::renderTitle(r.reference, title, paged ? 2 : 1, 128);
			expect(r.pbm()).to_equal(r.reference_pbm());
		};
		both_ways("MODULATION MATRIX", true);
		both_ways("MODULATION MATRIX", false);
		both_ways("FILTER", true);
		both_ways("ONE TWO THREE WORDS", true);
		both_ways("LFO 1", true);

		// The shortened form really is shorter -- the two renderings differ.
		uitest::Rig48 shortened;
		title_paged(shortened.frame, "MODULATION MATRIX", true);
		uitest::Rig48 whole;
		title_paged(whole.frame, "MODULATION MATRIX", false);
		expect(shortened.pbm() == whole.pbm()).to_be_false();

		// And no separator rule is drawn, unlike a plain menu title.
		bool rule = false;
		for (int32_t x = 0; x < 128; x++) {
			rule = rule || whole.lit(x, 17); // kTitleSeparatorY + the panel's hidden rows
		}
		expect(rule).to_be_false();
	});

	it("still draws the title and four placeholder frames for an empty span", _ {
		// renderOLED calls all three renderers unconditionally, and renderMenuItems with no items
		// falls straight through to the placeholder loop with current_x == 0 -- four dotted
		// frames under the title. totalPages is 0, so renderPageCounters draws nothing.
		uitest::Rig48 r;
		ColumnsState state{};
		const Response resp = columns(r.frame, r.frame.bounds(), state, std::span<const Column>{}, "FILTER");
		expect(resp.changed).to_be_false();
		expect(resp.entered).to_be_false();
		expect(static_cast<int32_t>(paging_of(std::span<const Column>{}, 0).total_pages)).to_equal(0);
		expect(static_cast<int32_t>(state.page)).to_equal(0);

		legacy::renderOLED(r.reference, "FILTER", std::span<const legacy::Item>{}, 0, 0, 0, 128, 48, r.frame.origin.y,
		                   r.style.invert_highlight, r.style.rounded);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/columns_empty_48.pbm", r.pbm());

		// The four frames, spelled out: each is drawn from `n * 32 + 7`, with dots along the top
		// at base_y + 1 (canvas row 20) and along the sides from `start_x - 2` / `end_x + 2`.
		for (int32_t n = 0; n < 4; n++) {
			const int32_t start_x = n * 32 + 7;
			expect(r.lit(start_x + 1, 20)).to_be_true();      // top edge
			expect(r.lit(start_x + 1, 41)).to_be_true();      // bottom edge, OLED height - 7
			expect(r.lit(start_x - 2, 23)).to_be_true();      // left edge, start_y + 3
			expect(r.lit(start_x + 17 + 2, 23)).to_be_true(); // right edge
		}
		// And no page counters in the top right.
		bool counters = false;
		for (int32_t x = 100; x < 128; x++) {
			for (int32_t y = 6; y < 15; y++) {
				counters = counters || r.lit(x, y);
			}
		}
		expect(counters).to_be_false();
	});

	it("reports the page of an out-of-range focus rather than a window over everything", _ {
		// `paging_of` is public and total; `columns` clamps identically before it calls in.
		std::vector<legacy::Item> six = four_columns();
		six.push_back(legacy::Item{.label = "DRIVE", .kind = legacy::Item::BAR, .norm = 0.75f});
		six.push_back(legacy::Item{.label = "MIX", .kind = legacy::Item::TOGGLE, .on = true});
		Page p{six};
		expect(static_cast<int32_t>(paging_of(p.span(), -3).page)).to_equal(0);
		expect(static_cast<int32_t>(paging_of(p.span(), 99).page)).to_equal(1);
		expect(static_cast<int32_t>(paging_of(p.span(), 99).focused_on_page)).to_equal(1);
	});
});
DELUGE_SPEC(ui_columns_spec, columns_w)
