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

/// @file
/// @brief The notification sink's installer. See `shell_notification_sink.h`.
///
/// Both entries are the legacy mapping `drain_notifications()` already uses for the same two notice
/// kinds (`gui/ui/notify_drain.h`), not a second transcription of it: the point of the sink is that
/// the shell's consumer and legacy's end in the same call.

#include "gui/ui/shell_notification_sink.h"

#include "gui/ui/notify_drain.h"

#include "shell/notices.h"

namespace deluge::gui::ui {
namespace {

const deluge::ui::shell::NotificationSink kNotificationSink{
    .show = &::showNoticeNotification,
    .cancel = &::cancelNoticeNotification,
};

} // namespace

void wireShellNotificationSink() {
	deluge::ui::shell::set_notification_sink(&kNotificationSink);
}

} // namespace deluge::gui::ui
