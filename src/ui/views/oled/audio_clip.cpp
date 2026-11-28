#include "ui/views/oled/audio_clip.h"

#include "ui/gfx/oled/font.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/oled/misc.h"
#include "ui/widgets/oled/scroll_text.h"
#include "ui/widgets/oled/text.h"

namespace deluge::ui::views::oled {

void audio_clip_name(widgets::oled::OledFrame& f, const AudioClipName& d, widgets::core::ScrollText& name_scroll,
                     widgets::core::ScrollText& info_scroll) {
	// **`name_empty` is deliberately false even when the name IS empty**, and this is a real difference between the
	// widget and this screen. `widgets::oled::output_name`'s `name_empty` branch draws a SLOT, which is legacy's
	// substitution for an unnamed **MIDI or CV** output (`views/view.cpp`). `drawOutputNameFromDetails`' name block is
	// one `if (!isNameEmpty) ... else if (outputType == MIDI_OUT) ... else if (outputType == CV)` chain, and an
	// `OutputType::AUDIO` output with no name matches **none** of the three -- so legacy draws the type text and
	// nothing else. Passing the empty name with `name_empty = false` is what reproduces that: the widget takes its
	// draw-the-name branch and draws an empty string. Measured against legacy in
	// `tests/integration/ui/scenes/audio_clip_scene_spec.cpp`'s "unnamed output" example, which fails with `name_empty
	// = name.empty()`.
	widgets::oled::output_name(f, f.bounds(),
	                           widgets::oled::OutputNameData{
	                               .type_text = d.type_text,
	                               .name = d.name,
	                               .slot = 0,
	                               .sub_slot = -1,
	                               .name_empty = false,
	                               .edited = true,
	                           });

	// The clip's second line is drawn only inside the `!isNameEmpty` arm, and only when a clip was passed -- the audio
	// clip view passes one. `output_name`'s header leaves this line to its caller.
	if (d.name.empty()) {
		return;
	}

	// The y is legacy's `yPos = yPos + 14` over the name's own y, which `output_name` picks from the same style value.
	const bool tall = f.style.menu_base_y == widgets::core::kMenuBaseY64;
	const int32_t name_y = f.bounds().y + (tall ? widgets::core::kOutputNameY64 : widgets::core::kOutputNameY48);

	// A name too wide for the panel scrolls, as legacy's side-scroller 0 does; one that fits stays as the widget
	// centred it.
	const int32_t name_h = widgets::oled::text_height_for(f.style.title_font);
	if (gfx::oled::string_width_px(d.name, name_h) > f.bounds().w) {
		(void)widgets::oled::scroll_text(f, gfx::oled::Rect{f.bounds().x, name_y, f.bounds().w, name_h}, d.name,
		                                 f.style.title_font, name_scroll);
	}

	// Centred when it fits; side-scroller 1 when it does not.
	const int32_t row_h = widgets::oled::text_height_for(f.style.row_font);
	const gfx::oled::Rect info_rect{f.bounds().x, name_y + 14, f.bounds().w, row_h};
	if (gfx::oled::string_width_px(d.info, row_h) > info_rect.w) {
		(void)widgets::oled::scroll_text(f, info_rect, d.info, f.style.row_font, info_scroll);
		return;
	}
	widgets::oled::text(f, info_rect, d.info,
	                    widgets::oled::TextStyle{.font = f.style.row_font, .align = widgets::oled::Align::Centre});
}

} // namespace deluge::ui::views::oled
