#pragma once
#include "param.h"

namespace deluge::gui::menu_item::reverb::plateau {
class PreDelay final : public Param {
public:
	using Param::Param;
	float get() override { return reverb().settings().pre_delay_time; }
	void set(float val) override { reverb().setPreDelay(val); }

	[[nodiscard]] float getMaxFloat() const override { return 0.5; }
};
} // namespace deluge::gui::menu_item::reverb::plateau
