#include "gui/ui/knob_focus_of.h"

#include "gui/views/knob_focus_from_stack.h"

namespace deluge::gui {

scenes::bind::KnobFocus knobFocusOf(::Song& song, const ModelStackWithThreeMainThings& stack) {
	return knobFocusFromStack(song, stack);
}

} // namespace deluge::gui
