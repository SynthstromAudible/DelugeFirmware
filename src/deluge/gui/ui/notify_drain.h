#pragma once
#include "notify/dirty_set.h"

struct LegacyRedraw {
	bool main_rows = false, sidebar_rows = false, oled = false;
};
LegacyRedraw legacy_redraw_for(const deluge::notify::DirtySet& d);

/// Temporary (until the UI shell owns the tick): map DirtySet bits to the legacy redraw calls
/// and NoticeQueue entries to display->. Called at the top of doAnyPendingUIRendering(). [task]
void drain_notifications();
