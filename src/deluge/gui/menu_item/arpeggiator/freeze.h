/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once
#include "gui/context_menu/freeze_arp.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/arp_freeze.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::arpeggiator {

/// Replaces the notes of the clip with the notes its arpeggiator plays, and switches the arpeggiator off, after asking
/// for confirmation. It's at the end of the arp pattern menu, also when the arpeggiator is off (then showing a dash),
/// so it's always found in the same place.
class Freeze final : public MenuItem {
public:
	using MenuItem::MenuItem;

	bool isRelevant(ModControllableAudio* /*modControllable*/, int32_t /*whichThing*/) const override {
		return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ArpFreeze) && !soundEditor.editingKitRow();
	}

	MenuItem* selectButtonPress() override {
		if (!isAvailable()) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_ARP_IS_OFF));
		}
		else {
			auto& confirmation = context_menu::getFreezeArp();
			confirmation.clip = getCurrentInstrumentClip();
			if (confirmation.setupAndCheckAvailability()) {
				openUI(&confirmation);
			}
		}
		return NO_NAVIGATION; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast): the cast is in the macro
	}

	bool shouldEnterSubmenu() override { return false; }

	[[nodiscard]] int32_t getOccupiedSlots() const override { return 2; }
	[[nodiscard]] bool showNotification() const override { return getCurrentUI() == &soundEditor; }
	void getNotificationValue(StringBuf& valueBuf) override { valueBuf.append(getValueText()); }
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		hid::display::OLED::main.drawStringCentered(getValueText(), slot.start_x,
		                                            slot.start_y + kHorizontalMenuSlotYOffset, kTextSpacingX,
		                                            kTextSpacingY, slot.width);
	}

private:
	static bool isAvailable() { return arp_freeze::isAvailable(getCurrentClip()); }
	static char const* getValueText() { return isAvailable() ? l10n::get(l10n::String::STRING_FOR_APPLY) : "-"; }
};

} // namespace deluge::gui::menu_item::arpeggiator
