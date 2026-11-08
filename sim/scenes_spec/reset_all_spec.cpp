/// @file
/// @brief The full factory reset from the ported settings menu re-applies, on the open guest, what main reads live.
///
/// A file of its own: `Deluge::factoryReset` rebuilds the MIDI singletons and rewrites the flash settings, which no
/// later example in the same process may inherit. The runner gives each file a fresh process and card copy.

#include "definitions_cxx.hpp"
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host_ui.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/screensaver.h"
#include "hid/led/pad_leds.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"

#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"

#include "support/host_flash.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace PadLEDs {
/// The pad dimmer's live level (`hid/led/pad_leds.cpp`), which `pad_leds.h` does not declare.
extern int32_t dimmerInterval;
} // namespace PadLEDs

namespace {
namespace spec = deluge::scenes::spec;
using deluge::scenes::menu_scene;
using deluge::scenes::context_menu::DescriptorId;
} // namespace

// clang-format off
describe reset_all("scenes::context_menu's full factory reset on the guest", $ {
	it("re-applies every live setting main reads, and leaves rounded corners as main does", _ {
		spec::ErasedFlash flash; // the reset programs the settings sector -- see `support/host_flash.h`
		const deluge::scenes::menu::MenuContext ctx{};
		FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::NO_INVERSION;
		deluge::gui::ui::pushShellHighlighting();
		FlashStorage::screensaverMode = ScreensaverMode::OFF;
		deluge::hid::display::Screensaver::settingsChanged();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
		shellHostUI.shell().lowering().set_shift_is_sticky(true);
		shellHostUI.shell().lowering().seed_shift(/*pressed=*/false, /*stuck=*/true, 0);
		menu_scene().set_horizontal_menus(false);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::RoundedCorners, RuntimeFeatureStateToggle::Off);
		deluge::gui::ui::pushShellRoundedCorners();
		constexpr int32_t kDimmed = 5;
		PadLEDs::setDimmerInterval(kDimmed);

		(void)deluge::scenes::context_menu::descriptor(DescriptorId::ResetAll).accept(ctx, 0);

		expect(shellHostUI.shell().surfaces().oled()[0].style->highlighting
		       == deluge::ui::Highlighting::PartialInversion)
		    .to_be_true();
		expect(shellHostUI.shell().screensaver_idle_ms())
		    .to_equal(static_cast<uint32_t>(kDefaultScreensaverTimeoutMinutes) * 60000u);
		expect(shellHostUI.shell().lowering().shift_is_sticky()).to_be_false();
		expect(shellHostUI.shell().lowering().sticky()).to_be_true(); // the latch outlives the reset, as in main
		expect(menu_scene().menu_context().horizontal_menus).to_be_true();
		expect(runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners)).to_be_true(); // the default
		expect(deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled).to_be_false();
		expect(shellHostUI.shell().surfaces().oled()[0].style->rounded).to_be_false();
		// Pad brightness is stale too: the stored default is reset, the dimmer keeps its level until the next boot.
		expect(PadLEDs::dimmerInterval).to_equal(kDimmed);
	});
});
// clang-format on

DELUGE_SPEC(reset_all_spec, reset_all)
