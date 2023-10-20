#pragma once
#include "param.h"

namespace deluge::gui::menu_item::reverb::plateau {
class ModDepth final : public Param {
public:
	using Param::Param;
	float get() override { return reverb().settings().mod_depth; }
	void set(float val) override { reverb().setTankModDepth(val); }

	[[nodiscard]] float getMaxFloat() const override { return 16.f; }
};
} // namespace deluge::gui::menu_item::reverb::plateau
