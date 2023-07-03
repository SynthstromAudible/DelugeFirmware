#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include <array>

namespace menu_item {
template <typename T> class SelectionForValue : public Selection {
	T& ref_;

public:
	SelectionForValue(T& value, char const* name = NULL) : ref_(value), Selection(name) {}
	void readCurrentValue() { soundEditor.currentValue = ref_; }
	void writeCurrentValue() { ref_ = soundEditor.currentValue; };
};

} // namespace menu_item
