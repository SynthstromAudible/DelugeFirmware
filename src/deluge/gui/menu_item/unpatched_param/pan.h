#pragma once
#include <cstdint>
#include "gui/menu_item/unpatched_param.h"

namespace menu_item::unpatched_param {

class Pan final : public UnpatchedParam {
public:
	Pan(char const* newName = 0, int newP = 0) : UnpatchedParam(newName, newP) {}
	void drawValue();

protected:
	int getMaxValue() const { return 32; }
	int getMinValue() const { return -32; }
	int32_t getFinalValue();
	void readCurrentValue();
};
} // namespace menu_item::unpatched_param
