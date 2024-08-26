#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::defaults {
class AccessibilityMenuHighlighting final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(FlashStorage::accessibilityMenuHighlighting); }
	void writeCurrentValue() override {
		FlashStorage::accessibilityMenuHighlighting = this->getValue<MenuHighlighting>();
	}
	vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {l10n::getView(l10n::String::STRING_FOR_FULL_INVERSION),
		        l10n::getView(l10n::String::STRING_FOR_PARTIAL_INVERSION),
		        l10n::getView(l10n::String::STRING_FOR_NO_INVERSION)};
	}
};
} // namespace deluge::gui::menu_item::defaults
