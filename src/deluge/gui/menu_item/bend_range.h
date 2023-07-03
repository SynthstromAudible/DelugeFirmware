#pragma once
#include "integer.h"

namespace menu_item {
class BendRange : public Integer {
public:
	BendRange(char const* newName = NULL) : Integer(newName) {}
	int getMaxValue() const { return 96; }
};
} // namespace menu_item
