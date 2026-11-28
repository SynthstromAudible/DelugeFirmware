#include "ui/views/oled/master.h"

#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/oled/scope.h"
#include "ui/widgets/oled/text.h"

namespace deluge::ui::views::oled {

void master_scope(widgets::oled::OledFrame& f, const MasterScope& d) {
	widgets::oled::title(f, d.title);
	widgets::oled::scope(f, detail::body_rect(f, 0), d.samples);
}

void master_scope_xy(widgets::oled::OledFrame& f, const MasterScopeXy& d) {
	widgets::oled::title_note(f, d.title, d.note);
	widgets::oled::scope_xy(f, detail::body_rect(f, 0), d.left, d.right);
}

} // namespace deluge::ui::views::oled
