#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "octave.h"

namespace deluge::gui::menu_item::tuning {
class TuningMenu final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override;
	void writeCurrentValue() override;
	static bool forClip();
	[[nodiscard]] std::string_view getTitle() const override;
	deluge::vector<std::string_view> getOptions(OptType optType) override;
	MenuItem* selectButtonPress() override;
};

} // namespace deluge::gui::menu_item::tuning
