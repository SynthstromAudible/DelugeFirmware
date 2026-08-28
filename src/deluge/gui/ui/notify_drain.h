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
