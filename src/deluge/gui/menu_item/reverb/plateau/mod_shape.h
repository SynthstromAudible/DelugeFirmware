#pragma once
#include "param.h"

namespace deluge::gui::menu_item::reverb::plateau {
class ModShape final : public Param {
public:
	using Param::Param;
	float get() override { return reverb().settings().mod_shape; }
	void set(float val) override { reverb().setTankModShape(val); }

	[[nodiscard]] int32_t getMaxValue() const override { return 16.f; }
};
} // namespace deluge::gui::menu_item::reverb::plateau
