#pragma once
#include "gui/menu_item/selection.h"
#include "playback/playback_handler.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::record {
class CountIn final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.countInEnabled; }
	void writeCurrentValue() { playbackHandler.countInEnabled = soundEditor.currentValue; }
};
} // namespace menu_item::record
