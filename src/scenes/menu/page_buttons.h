#pragma once

/// @file
/// @brief SYNTH/KIT/MIDI/CV/CROSS_SCREEN/SCALE on a horizontal page: their presses and their LEDs --
///        `HorizontalMenu::buttonAction` and `updateSelectedMenuItemLED`.

#include "scenes/menu/menu_core.h"
#include "scenes/menu/value_session.h"
#include "scenes/menu/walk.h"
#include "shell/scene.h"
#include "ui/core/led_frame.h"

#include <cstdint>
#include <optional>

namespace deluge::scenes::menu {

/// @brief The debounce shared by SYNTH/KIT/MIDI/CV/CROSS_SCREEN/SCALE on a horizontal page --
///        `HorizontalMenu::buttonAction`'s `navigation_buttons_debounce_threshold`
///        (`horizontal_menu.cpp:78`), 0.14 s as milliseconds. A press is accepted only when
///        strictly more time than this has passed since the last accepted one
///        (`MenuState::nav_press_ms`), matching legacy's `time - last > threshold` (`:81-82`)
///        rather than `>=`.
inline constexpr uint32_t kNavButtonDebounceMs = 140;

/// @brief One of the six buttons pressed on a horizontal page (`gui/menu_item/horizontal_menu.cpp:70-107`).
/// @return The result when the press was the page's, else nullopt.
std::optional<deluge::ui::shell::Result> handle_page_button(MenuCore core, MenuWalk& walk, ValueSession& session,
                                                            const deluge::ui::shell::UiEvent& event,
                                                            const deluge::ui::shell::UiContext& ui);

/// @brief Declare the six buttons' LEDs: lit over the focused column and with more than one page, off elsewhere.
void page_button_leds(const MenuContext& ctx, const MenuState& st, deluge::ui::LedFrame& f);

} // namespace deluge::scenes::menu
