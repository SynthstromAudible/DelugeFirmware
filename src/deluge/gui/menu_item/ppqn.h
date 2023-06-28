#pragma once
#include "gui/menu_item/integer.h"

namespace menu_item {
class PPQN : public Integer {
public:
  using Integer::Integer;
	int getMinValue() const { return 1; }
	int getMaxValue() const { return 192; }
};
}
