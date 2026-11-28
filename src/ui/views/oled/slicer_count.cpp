#include "ui/views/oled/slicer_count.h"

#include "ui/widgets/oled/popup.h"

namespace deluge::ui::views::oled {

void slicer_count(widgets::oled::OledFrame& f, const SlicerCount& d) {
	widgets::oled::count_window(f, d.label, d.digits);
}

} // namespace deluge::ui::views::oled
