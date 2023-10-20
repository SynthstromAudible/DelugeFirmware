#pragma once
#include "param.h"

namespace deluge::gui::menu_item::reverb::plateau {
class ModSpeed final : public Param {
public:
	using Param::Param;
	float get() override { return reverb().settings().mod_shape; }
	void set(float val) override { reverb().setTankModSpeed(val); }
};
} // namespace deluge::gui::menu_item::reverb::plateau
