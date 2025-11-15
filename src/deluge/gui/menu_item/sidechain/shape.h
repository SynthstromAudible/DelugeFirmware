#pragma once

#include <gui/menu_item/unpatched_param/updating_reverb_params.h>

namespace deluge::gui::menu_item::sidechain {
class Shape final : public unpatched_param::UpdatingReverbParams {
public:
	using UpdatingReverbParams::UpdatingReverbParams;

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		UpdatingReverbParams::configureRenderingOptions(options);
		options.label = l10n::get(l10n::String::STRING_FOR_SHAPE_SHORT);
	}
};
} // namespace deluge::gui::menu_item::sidechain
