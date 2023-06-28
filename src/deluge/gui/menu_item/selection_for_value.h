#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include <array>
#include <initializer_list>

namespace menu_item {
template <typename T> class SelectionForValue : public Selection {
	T& ref_;

public:
	SelectionForValue(T& value, char const* name = NULL) : ref_(value), Selection(name) {}
	void readCurrentValue() { soundEditor.currentValue = ref_; }
	void writeCurrentValue() { ref_ = soundEditor.currentValue; };
};

/* TODO: Once DisplayText is integrated
template <typename T, std::size_t N> class SelectionForValueWithOptions : public Selection {
	T& ref_;
	std::array<DisplayText, N> options_;

public:
	SelectionForValueWithOptions(T& value, std::initializer_list<DisplayText> options, char const* name = NULL)
	    : ref_(value), options_(options), Selection(name) {}
	void readCurrentValue() { soundEditor.currentValue = ref_; }
	void writeCurrentValue() { ref_ = soundEditor.currentValue; };
};
*/

} // namespace menu_item
