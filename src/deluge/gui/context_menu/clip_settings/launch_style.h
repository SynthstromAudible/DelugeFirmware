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

class LaunchStyle final : public ContextMenu {

public:
	LaunchStyle() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }

	Clip* clip = nullptr;

	/// Title
	char const* getTitle() override;

	/// Options
	Sized<const char**> getOptions() override;
};

extern LaunchStyle launchStyle;
} // namespace deluge::gui::context_menu::clip_settings
