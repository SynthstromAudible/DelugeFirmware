#pragma once

#include "gui/context_menu/context_menu.h"

class Clip;

namespace deluge::gui::context_menu::clip_settings {

class ClipSettingsMenu final : public ContextMenu {

public:
	ClipSettingsMenu() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }
	bool acceptCurrentOption() override; // If returns false, will cause UI to exit

	Clip* clip = nullptr;

	/// Title
	char const* getTitle() override;

	/// Options
	Sized<const char**> getOptions() override;

	ActionResult padAction(int32_t x, int32_t y, int32_t on) override;
};

extern ClipSettingsMenu clipSettings;
} // namespace deluge::gui::context_menu::clip_settings
