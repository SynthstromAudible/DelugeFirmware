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
#include "drivers/usb/usb_audio.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "model/output.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"
#include <algorithm>
#include <array>

namespace deluge::gui::menu_item::usb {

/// Whether the Deluge offers USB audio to a computer, and how: off, stereo, or multitrack. The host only reads our
/// descriptors when we connect, so this takes effect the next time USB starts up - at power-on
class Audio final : public Selection {
public:
	using Selection::Selection;
	// Shown in this order, which isn't how they're stored
	static constexpr std::array<usb_audio::Mode, usb_audio::kNumModes> kModes = {
	    usb_audio::Mode::OFF, usb_audio::Mode::STEREO, usb_audio::Mode::MULTITRACK};

	void readCurrentValue() override {
		this->setValue(std::find(kModes.begin(), kModes.end(), FlashStorage::usbAudioMode) - kModes.begin());
	}
	void writeCurrentValue() override {
		FlashStorage::usbAudioMode = kModes[this->getValue()];
		if (FlashStorage::usbAudioMode != activeMode()) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_RESTART_TO_APPLY));
		}
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_OFF), l10n::getView(STRING_FOR_STEREO), l10n::getView(STRING_FOR_MULTITRACK)};
	}

private:
	static usb_audio::Mode activeMode() {
		return usb_audio::multitrackActive() ? usb_audio::Mode::MULTITRACK
		       : usbAudioActive()            ? usb_audio::Mode::STEREO
		                                     : usb_audio::Mode::OFF;
	}
};

/// Whether audio from the host is mixed straight into the Deluge's outputs
class DirectMonitoring final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->setValue(usb_audio::directMonitoring); }
	void writeCurrentValue() override { usb_audio::directMonitoring = this->getValue(); }
};

/// What the host receives from the Deluge: its main mix, or its line input
class OutputSource final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(usb_audio::outputSource); }
	void writeCurrentValue() override { usb_audio::outputSource = this->getValue<usb_audio::OutputSource>(); }
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_MAIN_MIX), l10n::getView(STRING_FOR_LINE_IN)};
	}
};

/// Which multitrack USB bus the current track goes to instead of the main mix - or none. Per track, so shared by all
/// its clips. Only there while multitrack USB audio is on - and not for a kit's row, whose menu is the synth's: the
/// whole kit is routed, from its affect-entire menu
class TrackBus final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(getCurrentOutput()->getUSBBus()); }
	void writeCurrentValue() override { getCurrentOutput()->setUSBBus(this->getValue()); }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return usb_audio::multitrackActive() && !soundEditor.editingKitRow();
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		static_assert(usb_audio::kNumTrackBuses == 3);
		return {l10n::getView(STRING_FOR_MAIN_MIX), l10n::getView(STRING_FOR_USB_BUS_1),
		        l10n::getView(STRING_FOR_USB_BUS_2), l10n::getView(STRING_FOR_USB_BUS_3)};
	}
};

} // namespace deluge::gui::menu_item::usb
