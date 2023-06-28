#pragma once
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::arpeggiator {
class Gate final : public UnpatchedParam {
public:
	Gate(char const* newName = NULL, int newP = 0) : UnpatchedParam(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return !soundEditor.editingCVOrMIDIClip(); }
};

} // namespace menu_item::arpeggiator
