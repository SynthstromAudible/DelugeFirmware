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
/// @brief The **pad** half of the seam `shell_choice_dispatch.h` is the noun half of and
///        `shell_action_dispatch.h` is the verb half of -- what a grid pad the ported menu did not
///        claim is forwarded to.
///
/// The producing half is `ShellHostUI::padAction`: a later task calls `forwardGridPad` below for a
/// pad `MenuScene::grid_pad_claim()` (`shell/scene.h`, Task 1) did not claim. This file is the whole
/// of the consuming half, ported from `SoundEditor::padAction`'s root switch
/// (`gui/ui/sound_editor.cpp:1474-1517`) and its tail (`:1520-1545`) -- the part of that function
/// that runs only once `potentialShortcutPadAction` (`:1468`) has already said no.
///
/// **What deliberately stays on the other side of the wall.** The two note-editor arms inside the
/// `instrumentClipView` case (`:1481-1498`) are keyed on `SoundEditor::inNoteEditor()` /
/// `inNoteRowEditor()`, which read a `SoundEditor` member no guest path writes -- see the plan's own
/// ruling. The firmware-version diagnostics inside the tail (`:1521-1543`) are this task's own
/// deliberate gap -- ported behaviour the plan's task 2 brief did not ask this switch to carry,
/// named here rather than silently dropped. The `automationView` arm (`:1507-1512`) is **not** a
/// gap: task 3 ports it, as `PadForwardTarget::Automation` -- see `menuShowsAutomationParam` in the
/// `.cpp` for why that arm asks the ported menu rather than calling
/// `SoundEditor::handleAutomationViewPadAction` directly. None of the six arms below asks anything
/// Task 1's `GridPadClaim` answers -- that question is Task 4's,
/// asked by the caller before this function is ever reached.
///
/// @par Why this is its own translation unit, and why it is in `gui/ui/`
///
/// The same two reasons `shell_choice_dispatch.h` and `shell_action_dispatch.h` give for the same
/// shape. **In `gui/ui/`** because deciding an arm is exactly the half that gets to name
/// `keyboardScreen`, `instrumentClipView` and `performanceView` -- the symbols `src/scenes` may not
/// include (`scenes-includes-outside`, `scripts/check_layer_rules.py`). **Its own translation unit**
/// for the same `deluge_scenes` closure reason `shell_action_dispatch.h` gives.

#include <cstdint>

namespace deluge::gui::ui {

/// @brief Where a pad the ported menu did not claim was sent.
///
/// One value per arm of `SoundEditor::padAction`'s root switch (`gui/ui/sound_editor.cpp:1474-1517`)
/// plus its tail (`:1520-1545`). Returned so a caller -- and a spec -- can see the decision the
/// switch made without inspecting what it did.
enum class PadForwardTarget : uint8_t {
	/// No arm matched and the tail's conditions did not hold. Legacy returns `DEALT_WITH` here
	/// too (`:1548`): the pad is swallowed, which is what an open menu should do with it.
	None,
	/// `:1475` -- `keyboardScreen.padAction`, every coordinate.
	Keyboard,
	/// `:1500-1502` -- `instrumentClipView.padAction`, the audition column only.
	AuditionColumn,
	/// `:1516` -- `performanceView.padAction`, every coordinate, never reaching the tail.
	Performance,
	/// `:1508` -- `SoundEditor::handleAutomationViewPadAction`'s work, done here: see
	/// `menuShowsAutomationParam` in the `.cpp` for why it is not a call into that function.
	Automation,
	/// `:1545` -- `exitCompletely()`, ported as `closePortedMenu()`.
	Exit,
};

/// @brief What performs a decided forward. See `setPadForwardPerform`.
using PadForwardPerform = void (*)(PadForwardTarget target, int32_t x, int32_t y, int32_t velocity);

/// @brief Route one grid pad the ported menu did not claim, exactly as `SoundEditor::padAction` does.
/// @param x,y The pad, in `MatrixDriver` coordinates.
/// @param velocity Legacy's `on`: non-zero is a press.
/// @return Which arm matched -- already performed when this returns.
PadForwardTarget forwardGridPad(int32_t x, int32_t y, int32_t velocity);

/// @brief Replace what performs a decided forward; `nullptr` restores the real one.
///
/// `shell_choice_dispatch.h`'s `setSampleClaimDispatch` for the same reason it exists there: the
/// decision is the part worth pinning, and performing it means calling into `keyboardScreen` /
/// `instrumentClipView` / `performanceView`, whose handlers need model state a spec should not have
/// to build. `deluge_scenes_spec` links `deluge_app`, so those symbols resolve -- this is about what
/// running them costs, not about what links.
void setPadForwardPerform(PadForwardPerform perform);

} // namespace deluge::gui::ui
