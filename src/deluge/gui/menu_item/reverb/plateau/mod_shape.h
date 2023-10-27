#pragma once
#include "param.h"

namespace deluge::gui::menu_item::reverb::plateau {
class ModShape final : public Param {
public:
	using Param::Param;
	float get() override { return reverb().settings().mod_shape; }
	void set(float val) override { reverb().setTankModShape(val); }
};
} // namespace deluge::gui::menu_item::reverb::plateau
