#pragma once
#include "param.h"

namespace deluge::gui::menu_item::reverb::plateau {
class TankLPF final : public Param {
public:
	using Param::Param;
	float get() override { return reverb().settings().tank_hpf; }
	void set(float val) override { reverb().setTankFilterLowCutFrequency(val); }

	[[nodiscard]] float getMaxFloat() const override { return 10.f; }
};
} // namespace deluge::gui::menu_item::reverb::plateau
