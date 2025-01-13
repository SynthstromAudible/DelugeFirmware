/*
 * ContextMenu\Clip_Settings\Launch_Style.h
 *
 *  Created on: 11 Jun 2023
 *      Author: Robin
 */

#pragma once

#include "gui/context_menu/context_menu.h"

class Clip;

namespace deluge::gui::context_menu::clip_settings {

class LaunchStyleMenu final : public ContextMenu {

public:
	LaunchStyleMenu() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }

	Clip* clip = nullptr;

	/// Title
	char const* getTitle() override;

	/// Options
	std::span<const char*> getOptions() override;
};

extern LaunchStyleMenu launchStyle;
} // namespace deluge::gui::context_menu::clip_settings
