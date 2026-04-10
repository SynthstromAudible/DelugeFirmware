#pragma once

#include <gui/menu_item/unpatched_param/updating_reverb_params.h>

namespace deluge::gui::menu_item::sidechain {
class GlobalVolume final : public unpatched_param::UpdatingReverbParams {
public:
	using UpdatingReverbParams::UpdatingReverbParams;

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return SIDECHAIN_DUCKING; }

	void getColumnLabel(StringBuf& label) override {
		label.append(deluge::l10n::get(l10n::String::STRING_FOR_VOLUME_DUCKING_SHORT));
	}
};
} // namespace deluge::gui::menu_item::sidechain
