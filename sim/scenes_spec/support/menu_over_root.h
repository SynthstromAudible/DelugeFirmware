#pragma once

/// @file
/// @brief The ported menu opened over a chosen legacy root UI through the real shortcut entry, and the two pieces of
///        legacy state the examples that do so hold for their length: the `ShellMenu` toggle and Shift.
///
/// Shared by `pad_forward_spec.cpp` and `automation_retarget_spec.cpp`. Pair each with `GlobalGuestGuard` and
/// `UiStackGuard` (`global_guest_menu.h`): the entry opens the menu on the singleton guest and puts the guest on
/// legacy's UI stack.

#include "gui/ui/ui.h" // setRootUILowLevel
#include "gui/views/automation_view.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "util/shell_menu_entry.h" // openShellMenuAtShortcut

#include <cstdint>

/// @name Legacy's shift state, reached by bare `extern`: `Buttons::isShiftButtonPressed` reads these
/// (`hid/buttons.cpp`)
///       and the header does not export them.
/// @{
namespace Buttons {
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons
/// @}

namespace deluge::scenes::spec {

/// @brief Turn the `ShellMenu` toggle on for an example, and put it back however it ends.
struct ShellMenuOn {
	RuntimeFeatureStateToggle saved =
	    static_cast<RuntimeFeatureStateToggle>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShellMenu));
	ShellMenuOn() { runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShellMenu, RuntimeFeatureStateToggle::On); }
	~ShellMenuOn() { runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShellMenu, saved); }
	ShellMenuOn(const ShellMenuOn&) = delete;
	ShellMenuOn& operator=(const ShellMenuOn&) = delete;
};

/// @brief Hold Shift on legacy's side for an example, and let go however it ends.
struct ShiftHeld {
	ShiftHeld() { Buttons::shiftCurrentlyPressed = true; }
	~ShiftHeld() {
		Buttons::shiftCurrentlyPressed = false;
		Buttons::shiftCurrentlyStuck = false;
	}
	ShiftHeld(const ShiftHeld&) = delete;
	ShiftHeld& operator=(const ShiftHeld&) = delete;
};

/// @brief Open the ported menu over @p root at the sound shortcut (@p x, @p y) -- the page that lists the shortcut's
///        param, focused on it, as `SoundEditor::setup` lands one.
///
/// The application's node numbering is put back first: `open_menu_on_global_guest` numbers `root_song` alone, and the
/// shortcut entry refuses a node numbered 0.
inline bool open_shortcut_over(RootUI* root, InstrumentClip* clip, int32_t x, int32_t y) {
	(void)menu::assign_node_ids(menu::nodes::kAllRoots);
	setRootUILowLevel(root);
	return deluge::gui::ui::openShellMenuAtShortcut(clip, x, y, false, false, false, false);
}

/// @brief `open_shortcut_over` with the automation view as the root.
inline bool open_shortcut_over_automation(InstrumentClip* clip, int32_t x, int32_t y) {
	return open_shortcut_over(&automationView, clip, x, y);
}

} // namespace deluge::scenes::spec
