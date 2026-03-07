#pragma once

#include "gui/context_menu/context_menu.h"

class Clip;

namespace deluge::gui::context_menu::clip_settings {

class TempoRatioMenu final : public ContextMenu {

public:
	TempoRatioMenu() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }

	Clip* clip = nullptr;

	/// Title
	char const* getTitle() override;

	/// Options
	std::span<const char*> getOptions() override;
};

extern TempoRatioMenu tempoRatio;
} // namespace deluge::gui::context_menu::clip_settings
