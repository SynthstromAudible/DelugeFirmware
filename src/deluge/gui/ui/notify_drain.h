#pragma once
#include "notify/dirty_set.h"

/// @brief Which of the legacy redraw calls a drained DirtySet requires.
struct LegacyRedraw {
	bool main_rows = false, sidebar_rows = false, oled = false;
};

/// @brief Map a drained DirtySet to the legacy redraw calls it implies.
/// @param d The drained dirty set to inspect.
/// @return Which redraw calls to make.
LegacyRedraw legacy_redraw_for(const deluge::notify::DirtySet& d);

/// @brief Bridge onto the legacy renderers: drains the global DirtySet and NoticeQueue and
///        dispatches them to the legacy redraw calls and display-> calls.
///
/// Called at the top of doAnyPendingUIRendering(). [task]
void drain_notifications();

/// @brief `Notice::Kind::Notification`'s legacy mapping: `display->displayNotification(title, value)`,
///        `HorizontalMenu::displayNotification`'s own call (`gui/menu_item/horizontal_menu.cpp:342`).
///
/// Both consumers of the notice queue end here: `drain_notifications()` calls it directly, and the
/// shell's `Notices::drain` reaches it through the `NotificationSink` `wireShellNotificationSink()`
/// installs (`gui/ui/shell_notification_sink.h`) -- so which of them pops a notification does not
/// change the call. [task]
/// @param title The NUL-terminated title (legacy's `param_title`).
/// @param value The NUL-terminated value; empty for a title-only notification.
void showNoticeNotification(const char* title, const char* value);

/// @brief `Notice::Kind::CancelNotification`'s legacy mapping: the guarded `cancelPopup()`
///        `HorizontalMenu::switchVisiblePage`/`endSession` make (`horizontal_menu.cpp:363-365`,
///        `:587-589`). Reached from both consumers, as `showNoticeNotification` is. [task]
void cancelNoticeNotification();
