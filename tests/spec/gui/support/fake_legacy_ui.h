#pragma once
#include <cstdint>
#include <vector>

class UI;

/// @brief Test doubles for the parts of `gui/ui/ui.cpp` a legacy `UI` subclass needs to exist at
///        all.
///
/// The host spec suite deliberately never builds legacy's `deluge_app` closure (the note at the top
/// of `tests/spec/CMakeLists.txt`), and `gui/ui/ui.cpp` is squarely inside it: its five out-of-line
/// `UI` virtuals forward to `view`, `getRootUI()` and `renderUIsForOled()`, which between them drag
/// in the model, the playback engine and every legacy view. But a subclass's *vtable* names all
/// five, so nothing deriving from `UI` links without them.
///
/// So this file defines exactly those five plus `UI::UI` and `UI::close`, the same arrangement as
/// `gui/support/fake_legacy_hid.cpp` (the HID buffers) and `scenes/support/fake_shortcuts.cpp`
/// (the menu shortcut tables). Each one records that it was called and does nothing else, which is
/// what makes them observable: a spec can assert that `ShellHostUI::exitUI` closed the guest, which
/// is a fact about `closeUI(this)` and cannot be seen from inside the shell.
///
/// It also defines `deluge::scenes::scene_registry()`, for the same reason and not the same shape:
/// `shell_host_ui.cpp` defines the one `shellHostUI` global over the application's real scene table,
/// and that table odr-uses every ported scene -- the whole `deluge_scenes` closure this suite does
/// not build. The fake returns an empty registry, so the global exists and is inert; every example
/// builds its own `ShellHostUI` over its own table.
namespace fakelegacyui {

/// @name Recorders
/// @{
/// Every `closeUI(ui)` call, in order.
extern std::vector<UI*> closed;
/// How many times `renderUIsForOled()` was asked for a redraw.
extern int oled_redraws;
/// Every `UI::modEncoderAction` / `modButtonAction` / `modEncoderButtonAction` / `graphicsRoutine`
/// call, as a running count -- the four base implementations a guest must not silently inherit
/// without meaning to.
extern int mod_encoder_actions;
extern int mod_button_actions;
extern int mod_encoder_button_actions;
extern int graphics_routines;
/// @}

/// @brief Clear every recorder.
void reset();

} // namespace fakelegacyui
