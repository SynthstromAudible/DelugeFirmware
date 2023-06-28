#pragma once
#include "gui/menu_item/patched_param/integer.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::arpeggiator {
class Rate final : public patched_param::Integer {
public:
	Rate(char const* newName = NULL, int newP = 0) : Integer(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return !soundEditor.editingCVOrMIDIClip(); }
};

} // namespace menu_item::arpeggiator
