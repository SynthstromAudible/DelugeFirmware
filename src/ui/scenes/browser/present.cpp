// What the browser's two surfaces show, as the plain structs the views draw. Const members: nothing here writes the
// scene or reads a frame.
#include "project/device_settings.h"
#include "ui/gfx/oled/font.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/grid/favourites.h"
#include "ui/widgets/grid/keyboard.h"
#include "ui/widgets/grid/shortcut_blink.h"
#include "ui/widgets/oled/text.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::ui::scenes::browser {

static_assert(deluge::ui::widgets::core::kDelugeStyle48.menu_rows <= static_cast<int32_t>(kMaxBrowserRows)
                  && deluge::ui::widgets::core::kDelugeStyle64.menu_rows <= static_cast<int32_t>(kMaxBrowserRows),
              "the list's storage holds every shipped style's rows");

namespace {

/// @brief How many of @p name's bytes can ink a row @p room px wide: every byte up to the first whose glyph starts a
///        cell past the row's right edge, the pen laid out as `string_width_px` lays it (width, spacing, kerning).
///        The cell of slack covers a kerning pull back across the edge. A byte with no glyph advances nothing in the
///        row font, so a name of them leaves its visible tail in the count however long it is.
size_t visible_bytes(std::string_view name, int32_t room, int32_t height) {
	int32_t pen = 0;
	uint8_t previous = 0;
	for (size_t k = 0; k < name.size(); k++) {
		const auto c = static_cast<uint8_t>(name[k]);
		if (k > 0) {
			pen += ui::gfx::oled::kerning_px(previous, c, height);
		}
		if (pen > room + widgets::core::kTextSpacingX) {
			return k;
		}
		pen += ui::gfx::oled::char_width_px(c, height) + ui::gfx::oled::char_spacing_px(c, height, false);
		previous = c;
	}
	return name.size();
}

} // namespace

views::oled::BrowserList BrowserScene::present_list(const widgets::core::Style& style, int32_t width,
                                                    std::span<views::oled::BrowserRow> rows, BrowserText& text) const {
	const Descriptor& d = descriptor(id_);
	// Legacy reads `display->getNumBrowserAndMenuLines()`, which is a hard-coded 3 on every panel
	// (`hid/display/oled.h`); the port reads `Style::menu_rows`, which is 3 on the shipped
	// 48-row panel and 5 on a 64-row one. Same number where legacy runs today, and a taller panel
	// fills its rows instead of leaving two blank -- the same choice `list`/`menu_rect` already
	// made for the menu.
	const int32_t room = std::min(style.menu_rows < 1 ? 1 : style.menu_rows, static_cast<int32_t>(rows.size()));
	// "If we're currently typing a filename which doesn't (yet?) have a file..." (`Browser::renderOLED`): one
	// row, showing the edited text against the file icon.
	const int32_t drawn = (file_index_ == -1) ? std::min<int32_t>(1, room) : room;

	// With the cursor at the start a name too wide for the row side-scrolls (`OLED::setupSideScroller`, which legacy
	// arms only for a name that does not fit).
	const int32_t text_x = views::oled::browser_text_x(style);
	const int32_t height = widgets::oled::text_height_for(style.row_font);
	const bool scrolls = edit_.cursor == 0 && ui::gfx::oled::string_width_px(entered_text(), height) > width - text_x;

	const int32_t count = entry_count();
	size_t n = 0;
	for (int32_t o = 0; o < drawn; o++) {
		if (file_index_ == -1) {
			rows[n++] = {.label = entered_text(), .folder = false, .selected = true, .scrolls = scrolls};
			continue;
		}
		const int32_t i = o + scroll_;
		if (i >= count) {
			break;
		}
		const bind::CatalogueEntry item = *entry_at(i);
		const std::string_view name = row_label(item.name, item.folder);
		// The bytes that can ink the row are copied; a name whose visible part outgrows the copy (a long run of
		// bytes with no glyph before its tail) is drawn from the catalogue, which no render changes.
		std::array<char, kMaxEnteredChars>& label = text.labels[n];
		const size_t len = visible_bytes(name, width - text_x, height);
		std::string_view shown = name;
		if (len <= label.size()) {
			std::copy_n(name.begin(), len, label.begin());
			shown = std::string_view{label.data(), len};
		}
		const bool selected = i == file_index_;
		rows[n++] = {.label = shown, .folder = item.folder, .selected = selected, .scrolls = selected && scrolls};
	}
	return {.title = d.title,
	        .file_icon = d.file_icon,
	        .file_icon_pt2 = d.file_icon_pt2,
	        .entered = entered_text(),
	        .rows = rows.first(n)};
}

views::grid::KeyboardPads BrowserScene::present_keys() const {
	// `QwertyUI::drawKeys` (`qwerty_ui.cpp`). `DxSyxBrowser::opened` sets `qwertyVisible = false` and never draws the
	// keys: the pads still type, dark.
	return {.layout = layout_, .home_row = widgets::grid::kTextKeyboardHomeRow, .dark = id_ == BrowserId::DxSyx};
}

std::optional<views::grid::FavouritePads> BrowserScene::present_favourites(uint32_t now_ms) const {
	// The rows stay drawn with the keys once the keyboard is hidden, as `main`'s pads keep them; only the
	// flash and the presses need the keyboard visible.
	const project::FavouritesLayout layout = favourites_layout();
	if (id_ != BrowserId::Sample || layout == project::FavouritesLayout::OFF) {
		return std::nullopt;
	}
	// `QwertyUI::renderFavourites`, then `timerCallback`'s flash of the current favourite and bank every 500 ms from
	// the draw.
	const bind::Favourites& bank = favourites();
	views::grid::FavouritePads d{.row = favourite_row(),
	                             .colours = widgets::grid::favourite_row_colours(bank.colours()),
	                             .banks = std::nullopt,
	                             .flash_favourite = std::nullopt,
	                             .flash_bank = std::nullopt};
	if (layout == project::FavouritesLayout::FAVOURITES_AND_BANKS) {
		d.banks = widgets::grid::favourite_bank_colours();
	}
	// A flash restarts from the draw that finds no anchor, and that draw's period has nothing to flash.
	if (!favourites_visible() || !favourite_flash_anchor_ms_.has_value()) {
		return d;
	}
	if (favourite_flash_step_at(now_ms) != favourite_flash_step_) {
		d.flash_favourite = bank.current_favourite();
		if (banks_visible()) {
			d.flash_bank = bank.current_bank();
		}
	}
	return d;
}

views::grid::BlinkPads BrowserScene::present_browse_blink(uint32_t now_ms) const {
	// `SoundEditor::blinkShortcut` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) on the BROWSE pad of the source
	// being edited (column 0 for an audio clip): every cycle of legacy's blink counter for the file selector's blink,
	// every other for the browser's, counted from when the blink was armed.
	const uint32_t step = widgets::grid::blink_step(blink_anchor_ms_.value_or(now_ms), now_ms);
	return {.plan = widgets::grid::exclusive_blink(ctx_.is_audio_clip ? 0 : static_cast<uint8_t>(ctx_.source_index), 5,
	                                               browse_blink_ == BrowseBlink::Menu ? uint8_t{0} : uint8_t{1}),
	        .step = step,
	        .fire = step != blink_fired_step_};
}

} // namespace deluge::ui::scenes::browser
