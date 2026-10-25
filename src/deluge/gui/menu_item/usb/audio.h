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
#include "hid/display/display.h"
#include "model/output.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::usb {

/// Whether the Deluge offers USB audio to a computer. The host only reads our descriptors when we connect, so this
/// takes effect the next time USB starts up - at power-on
class Audio final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->setValue(FlashStorage::usbAudioEnabled); }
	void writeCurrentValue() override {
		FlashStorage::usbAudioEnabled = this->getValue();
		if (FlashStorage::usbAudioEnabled != usbAudioActive()) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_RESTART_TO_APPLY));
		}
	}
};

/// Whether audio from the host is mixed straight into the Deluge's outputs
class DirectMonitoring final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->setValue(usb_audio::directMonitoring); }
	void writeCurrentValue() override { usb_audio::directMonitoring = this->getValue(); }
};

/// What the host receives from the Deluge: its main mix, its line input, or the tracks set to go there
class OutputSource final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(usb_audio::outputSource); }
	void writeCurrentValue() override { usb_audio::outputSource = this->getValue<usb_audio::OutputSource>(); }
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_MAIN_MIX), l10n::getView(STRING_FOR_LINE_IN),
		        l10n::getView(STRING_FOR_TRACKS)};
	}
};

/// Where the current track's audio goes: the main mix, or USB instead. Per track, so shared by all its clips. Only
/// takes effect while the host is getting tracks
class Destination final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(getCurrentOutput()->getDestination()); }
	void writeCurrentValue() override { getCurrentOutput()->setDestination(this->getValue<TrackDestination>()); }
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_MAIN_MIX), l10n::getView(STRING_FOR_USB)};
	}
};

} // namespace deluge::gui::menu_item::usb
