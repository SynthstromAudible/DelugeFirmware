#pragma once
#include "integer.h"

namespace menu_item::patched_param {
class Pan : public Integer {
public:
	Pan(char const* newName = 0, int newP = 0) : Integer(newName, newP) {}
#if !HAVE_OLED
	void drawValue();
#endif
protected:
	int getMaxValue() { return 32; }
	int getMinValue() { return -32; }
	int32_t getFinalValue();
	void readCurrentValue();
};
} // namespace menu_item
