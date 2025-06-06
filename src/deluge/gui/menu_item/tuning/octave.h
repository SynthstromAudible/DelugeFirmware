#pragma once
#include "gui/menu_item/decimal.h"

namespace deluge::gui::menu_item::tuning {
class Octave final : public Decimal {
public:
	using Decimal::Decimal;
	void readCurrentValue() override {}
	void writeCurrentValue() override {}

	int32_t selectedNote;

	[[nodiscard]] virtual std::string_view getTitle() const override {
		return std::string_view(TuningSystem::tuning->name);
	}

protected:
	[[nodiscard]] int32_t getMinValue() const final { return -20000; }
	[[nodiscard]] int32_t getMaxValue() const final { return 20000; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 2; }
	virtual int32_t getDefaultEditPos() { return 2; }
};
} // namespace deluge::gui::menu_item::tuning
