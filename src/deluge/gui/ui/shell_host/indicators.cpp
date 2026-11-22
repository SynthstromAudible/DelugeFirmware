#include "gui/ui/shell_host/shell_host.h"
#include "hid/led/pad_leds.h"

namespace deluge::gui::shell_host {
namespace {

/// The pads' indicators, through `PadLEDs`.
class HostIndicators final : public deluge::ui::scenes::bind::Indicators {
public:
	void clear_tick_squares() override { PadLEDs::clearTickSquares(true); }
};

} // namespace

deluge::ui::scenes::bind::Indicators& indicators() {
	// Constant-initialised, so it links as `.data` into SDRAM with the host's other port objects (`sdram_sections.x`).
	static constinit HostIndicators host;
	return host;
}

} // namespace deluge::gui::shell_host
