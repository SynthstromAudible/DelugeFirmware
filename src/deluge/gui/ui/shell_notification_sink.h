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
/// @brief Installs `deluge::ui::shell::NotificationSink` -- the shell's `Notice::Kind::Notification`
///        consumer, pointed at legacy's own display popup slot.
///
/// This header names no `shell/` type, only the one no-arguments function that installs the sink --
/// the same one-symbol-wide seam `shell_note_bridge.h` draws (`scripts/check_layer_rules.py`'s
/// `LEGACY_PORT_SEAMS` comment on each). `shell_notification_sink.cpp` is where the `shell/`
/// include happens.

namespace deluge::gui::ui {

/// @brief Install the notification sink, once, at boot: `showNoticeNotification` and
///        `cancelNoticeNotification` (`gui/ui/notify_drain.h`), via
///        `deluge::ui::shell::set_notification_sink`.
///
/// Like `wireShellNoteBridge()` it holds function pointers and no `Song`, so it has no ordering
/// constraint of its own; nothing calls through it until a notice is drained.
void wireShellNotificationSink();

} // namespace deluge::gui::ui
