#pragma once
#include "param.h"

namespace deluge::gui::menu_item::reverb::plateau {
class InputLPF final : public Param {
public:
	using Param::Param;
	float get() override { return reverb().settings().input_hpf; }
	void set(float val) override { reverb().setInputFilterLowCutoffPitch(val); }

	[[nodiscard]] float getMaxFloat() const override { return 10.f; }
};
} // namespace deluge::gui::menu_item::reverb::plateau
