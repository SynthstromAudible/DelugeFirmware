#pragma once

/// @file
/// @brief `NoNotificationSink`: the shell's own `Notification` banner for an example's scope.

#include "shell/notices.h"

namespace deluge::scenes::spec {

/// @brief No notification sink for the scope, so the shell's own `Notification` banner shows a scene's notification;
///        the sink app init installs (`wireShellNotificationSink`) is put back after. For examples that read that
///        banner.
struct NoNotificationSink {
	const deluge::ui::shell::NotificationSink* saved = deluge::ui::shell::notification_sink();
	NoNotificationSink() { deluge::ui::shell::set_notification_sink(nullptr); }
	~NoNotificationSink() { deluge::ui::shell::set_notification_sink(saved); }
	NoNotificationSink(const NoNotificationSink&) = delete;
	NoNotificationSink& operator=(const NoNotificationSink&) = delete;
};

} // namespace deluge::scenes::spec
