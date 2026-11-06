/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief The automation view under the ported menu: what the view does when the menu moves, and the buttons it takes
///        from under the menu.
///
/// `MenuScene` files one record per tick of a change of its value node and its cause (`MenuScene::take_transition`,
/// `scenes/menu/transition.h`); this drains it after the tick, as `dispatchShellActions` drains the action outbox, and
/// runs what legacy's sound editor runs for the same change against `automationView`
/// (`SoundEditor::handlePotentialParamMenuChange` and `Automation::buttonAction`): select the menu's param in the view,
/// return the view to its overview, or leave its "menu view". CLIP, X_ENC and KEYBOARD reach the view the same way,
/// forwarded ahead of the menu. `src/scenes` may not name the view; this file may.

#include "definitions_cxx.hpp" // ActionResult
#include "hid/button.h"

#include <cstdint>
#include <optional>

namespace deluge::gui::ui {

/// @brief Collect the menu's transition from the last tick, if any, and act on it as legacy's sound editor does.
///
/// Over any root but the automation view it does nothing. Afterwards the guest re-reads its root facts
/// (`ShellHostUI::refreshRootFacts`): the greyout and the knobs follow the view between its overview and editor.
void dispatchMenuTransitions();

/// @brief CLIP, X_ENC and KEYBOARD under the menu, where the automation view takes them, as legacy's sound editor
///        does: CLIP on a param enters the view on it in "menu view" (or leaves it), X_ENC on a param over the view is
///        the view's, and KEYBOARD over the view leaves it for the keyboard screen.
/// @param b  The button.
/// @param on Its press (true) or release.
/// @return What legacy's handler answers when one of these takes the button; nullopt when none does, for the menu.
std::optional<ActionResult> forwardAutomationButton(deluge::hid::Button b, bool on);

/// @brief Whether the gold knobs' turns and pushes go to the automation view rather than the menu's knobs: it is the
///        root and in its editor (`SoundEditor::modEncoderAction` hands them to the view whenever it is the root; on
///        its overview the knobs' service does what the view would).
bool automationEditorTakesKnobs();

/// @brief A gold knob's turn into the automation view's editor, then the menu re-reads its value.
void forwardAutomationGoldTurn(int32_t whichModEncoder, int32_t offset);

/// @brief A gold knob's push into the automation view's editor, then the menu re-reads its value.
void forwardAutomationGoldPush(uint8_t whichModEncoder, bool on);

/// @brief `SoundEditor::selectEncoderAction`'s forward: with a note held in the automation view's editor, on the menu's
///        own param and not a multi-pad press, a select turn fine-tunes that step, five times as far with SHIFT.
/// @return Whether the view took the turn; false leaves it to the menu.
bool forwardAutomationSelectTurn(int8_t offset);

/// @brief A horizontal turn under the menu that legacy's current item hands to the root UI
///        (`MenuScene::route_horizontal_turn`): to the automation view or the keyboard screen.
/// @param offset The turn's detents.
/// @param now_ms The guest's clock, for a cable page's edge delay.
/// @return `DEALT_WITH` when the turn went to the root or nowhere; nullopt to feed it to the menu.
std::optional<ActionResult> forwardMenuHorizontal(int32_t offset, uint32_t now_ms);

/// @brief `SoundEditor::verticalEncoderAction`: a vertical turn under the menu goes to the root UI, unless SHIFT or
///        X_ENC is held, when it does nothing.
/// @return The root's answer, or `DEALT_WITH`; nullopt when the menu is not what takes input.
std::optional<ActionResult> forwardMenuVertical(int32_t offset);

} // namespace deluge::gui::ui
