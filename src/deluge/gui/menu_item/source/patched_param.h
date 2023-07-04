#pragma once
#include "gui/menu_item/patched_param/integer.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::source {
class PatchedParam : public menu_item::patched_param::Integer {
public:
	using Integer::Integer;
	uint8_t getP() { return menu_item::PatchedParam::getP() + soundEditor.currentSourceIndex; }
};
} // namespace menu_item::source
