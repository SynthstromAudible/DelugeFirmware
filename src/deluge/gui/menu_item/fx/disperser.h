/*
 * Copyright (c) 2024 Synthstrom Audible Limited
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

#include "dsp/disperser.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/zone_based.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "util/d_string.h"
#include <cstdint>
#include <string>

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::fx {

// Disperser Freq: Center frequency for allpass cascade (0-127, maps to 50Hz-8kHz)
class DisperserFreq final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->disperser.freq); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t current_value = this->getValue();

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->disperser.freq = current_value;
				}
			}
		}
		else {
			soundEditor.currentModControllable->disperser.freq = current_value;
		}
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 127; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return BAR; }
};

// Disperser Stages: Number of active allpass stages (0-8 or 0-32 with HiCPU enabled)
// CPU cost scales roughly linearly: s8 ≈ 2x reverb, s16 ≈ 4x, s24 ≈ 8x, s32 ≈ 10x+
// Higher stage counts (9-32) require DisperserHiCPU community feature to be enabled
// Secret menu: Push+twist encoder to adjust gammaPhase (offsets both topo and twist meta zones by 1024*gamma)
class DisperserStages final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->disperser.getStages()); }
	bool usesAffectEntire() override { return true; }

	// Show CPU indicator in notification when HiCPU mode is enabled
	void getNotificationValue(StringBuf& value) override {
		int32_t val = this->getValue();
		if (val == 0) {
			value.append("OFF");
		}
		else if (runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::DisperserHiCPU)) {
			// HiCPU mode: show warnings for high stage counts
			if (val >= 24) {
				value.appendInt(val);
				value.append(" HiCPU!"); // CPU warning (very high)
			}
			else if (val >= 16) {
				value.appendInt(val);
				value.append(" HiCPU"); // CPU warning (high)
			}
			else {
				value.appendInt(val);
			}
		}
		else {
			value.appendInt(val);
		}
	}

	void writeCurrentValue() override {
		int32_t current_value = this->getValue();

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			bool anyFailed = false;
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					if (!soundDrum->disperser.setStages(current_value)) {
						anyFailed = true;
					}
				}
			}
			if (anyFailed) {
				display->displayPopup("RAM!");
			}
		}
		else {
			if (!soundEditor.currentModControllable->disperser.setStages(current_value)) {
				// Allocation failed - show error and revert to 0
				display->displayPopup("RAM!");
				this->setValue(0);
			}
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust gammaPhase (adds 1024*gamma to both topo and twist meta zones)
			Buttons::selectButtonPressUsedUp = true;
			float& gamma = soundEditor.currentModControllable->disperser.phases.gammaPhase;
			gamma = std::max(0.0f, gamma + static_cast<float>(offset) * 0.1f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "G:%d", static_cast<int32_t>(gamma * 10.0f));
			display->displayPopup(buffer);
			renderUIsForOled(); // Refresh display to show updated coordinate format
			suppressNotification_ = true;
		}
		else {
			IntegerWithOff::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	// Max stages: 8 normally, 32 with HiCPU community feature enabled
	[[nodiscard]] int32_t getMaxValue() const override {
		return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::DisperserHiCPU) ? 32 : 8;
	}
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return BAR; }

	// Show "OFF" when stages=0 (effect bypassed)
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		if (this->getValue() == 0) {
			deluge::hid::display::OLED::main.drawStringCentered("OFF", slot.start_x,
			                                                    slot.start_y + kHorizontalMenuSlotYOffset,
			                                                    kTextSpacingX, kTextSpacingY, slot.width);
			return;
		}
		IntegerWithOff::renderInHorizontalMenu(slot);
	}

private:
	mutable bool suppressNotification_ = false;
};

/**
 * Disperser Topology zone control - 8 zones with discrete signal routings
 *
 * Zone 0: Cascade - stages in series (current default)
 * Zone 1: Ladder - progressive cross-coupling through cascade
 * Zone 2: Owlpass - stages cluster into two frequency groups (formant-like)
 * Zone 3: Cross-Coupled - L↔R feedback mixing between stages
 * Zone 4: Parallel - two cascades in parallel for thick chorus character
 * Zone 5: Nested - Schroeder-style nested allpass structure
 * Zone 6: Diffuse - randomized per-stage coefficient variation
 * Zone 7: Spring - chirp/spring reverb character
 *
 * Secret menu: Push+twist encoder to adjust topoPhaseOffset
 * Press encoder (no twist): Opens mod matrix source selection
 */
class DisperserTopo final : public ZoneBasedDualParam<params::GLOBAL_DISPERSER_TOPO> {
public:
	using ZoneBasedDualParam::ZoneBasedDualParam;

	// Field accessors for ZoneBasedPatchedParam (sync with disperser.topo)
	[[nodiscard]] q31_t getFieldValue() const override {
		return soundEditor.currentModControllable->disperser.topo.value;
	}
	void setFieldValue(q31_t value) override { soundEditor.currentModControllable->disperser.topo.value = value; }

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "Cascade";
		case 1:
			return "Ladder";
		case 2:
			return "Owlpass";
		case 3:
			return "Cross";
		case 4:
			return "Parallel";
		case 5:
			return "Nested";
		case 6:
			return "Diffuse";
		case 7:
			return "Spring";
		default:
			return "?";
		}
	}

	[[nodiscard]] const char* getShortZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "CA";
		case 1:
			return "LA";
		case 2:
			return "BI";
		case 3:
			return "CR";
		case 4:
			return "PA";
		case 5:
			return "NE";
		case 6:
			return "DI";
		case 7:
			return "SP";
		default:
			return "??";
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust topoPhaseOffset (gated ≥0 for fast floor optimization)
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->disperser.phases.topoPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 0.1f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "offset:%d", static_cast<int32_t>(phase * 10.0f));
			display->displayPopup(buffer);
			renderUIsForOled(); // Refresh display for consistency
			suppressNotification_ = true;
		}
		else {
			ZoneBasedDualParam::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

private:
	mutable bool suppressNotification_ = false;
};

/**
 * Disperser Twist (character) zone control - 8 zones with character modifiers
 *
 * Maximum chirp architecture - transient emphasis for bigger chirps.
 *
 * Zone 0: Width - Stereo spread via L/R frequency offset
 * Zone 1: Punch - Transient emphasis before dispersion (bigger chirps!)
 * Zone 2: Curve - Frequency distribution (low cluster → linear → high cluster)
 * Zone 3: Chirp - Transient-triggered delay for chirp echoes
 * Zone 4: QTilt - Q varies across stages (uniform → high sharp → low sharp)
 * Zones 5-7: Meta - All effects combined with φ-triangle evolution
 *
 * Secret menu: Push+twist encoder to adjust twistPhaseOffset
 * Press encoder (no twist): Opens mod matrix source selection
 */
class DisperserTwist final : public ZoneBasedDualParam<params::GLOBAL_DISPERSER_TWIST> {
public:
	using ZoneBasedDualParam::ZoneBasedDualParam;

	// Field accessors for ZoneBasedPatchedParam (sync with disperser.twist)
	[[nodiscard]] q31_t getFieldValue() const override {
		return soundEditor.currentModControllable->disperser.twist.value;
	}
	void setFieldValue(q31_t value) override { soundEditor.currentModControllable->disperser.twist.value = value; }

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "Width";
		case 1:
			return "Punch"; // Transient boost → bigger chirps
		case 2:
			return "Curve"; // Bipolar freq distribution (low→linear→high)
		case 3:
			return "Chirp"; // Transient-triggered delay echoes
		case 4:
			return "QTilt"; // Q varies across stages
		case 5:
			return "Twist1"; // Meta zone 1
		case 6:
			return "Twist2"; // Meta zone 2
		case 7:
			return "Twist3"; // Meta zone 3
		default:
			return "---";
		}
	}

	[[nodiscard]] const char* getShortZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "WD";
		case 1:
			return "PU"; // Punch
		case 2:
			return "CV"; // Curve
		case 3:
			return "CH"; // Chirp
		case 4:
			return "QT"; // Q Tilt
		default:
			return "TW"; // Twist zones 5-7
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust twistPhaseOffset (gated ≥0 for fast floor optimization)
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->disperser.phases.twistPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 0.1f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "offset:%d", static_cast<int32_t>(phase * 10.0f));
			display->displayPopup(buffer);
			renderUIsForOled(); // Refresh display to show updated coordinate format
			suppressNotification_ = true;
		}
		else {
			ZoneBasedDualParam::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	// Override rendering to show numeric coordinates when phaseOffset > 0
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		double phaseOffset = soundEditor.currentModControllable->disperser.phases.effectiveMeta();
		if (phaseOffset != 0.0) {
			// When secret knob is engaged, show "P:Z" (phase:zone) with visual indicator
			cacheCoordDisplay(phaseOffset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kDisperserResolution, kDisperserNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kDisperserResolution, kDisperserNumZones,
			                           [this](int32_t z) { return this->getZoneName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		double phaseOffset = soundEditor.currentModControllable->disperser.phases.effectiveMeta();
		if (phaseOffset != 0.0) {
			// When secret knob is engaged, show numeric coordinates
			cacheCoordDisplay(phaseOffset, this->getValue());
			drawZoneForOled(this->getValue(), kDisperserResolution, kDisperserNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kDisperserResolution, kDisperserNumZones,
			                [this](int32_t z) { return this->getZoneName(z); });
		}
	}

private:
	mutable bool suppressNotification_ = false;

	// Resolution and zone count for disperser (1024 steps, 8 zones)
	static constexpr int32_t kDisperserResolution = 1024;
	static constexpr int32_t kDisperserNumZones = 8;

	// Static storage for coordinate display (used by getCoordName callback)
	static inline char coordBuffer_[12] = {};
	static void cacheCoordDisplay(double phaseOffset, int32_t value) {
		// Format: "P:Z" where P=phaseOffset (int), Z=zone index (0-7)
		// 128 encoder clicks = 1 zone, so Z increments once per zone traversal
		int32_t p = static_cast<int32_t>(phaseOffset);
		int32_t z = value >> 7; // 0-1023 → 0-7 (zone index)
		snprintf(coordBuffer_, sizeof(coordBuffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coordBuffer_; }
};

} // namespace deluge::gui::menu_item::fx
