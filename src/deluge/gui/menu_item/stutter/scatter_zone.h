/*
 * Copyright © 2024-2025 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */
#pragma once

#include "gui/menu_item/zone_based.h"
#include "gui/ui/sound_editor.h"
#include "model/fx/stutterer.h"
#include "modulation/params/param.h"
#include <hid/buttons.h>
#include <hid/display/display.h>

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::stutter {

// Resolution and zone count for scatter zone params (1024 steps, 8 zones)
static constexpr int32_t kScatterResolution = 1024;
static constexpr int32_t kScatterNumZones = 8;

/**
 * Scatter Zone A - Structural control
 *
 * Zone 0: Drift - Sequential with slight offset
 * Zone 1: Swap - Adjacent pair swapping
 * Zone 2: Retro - Reverse order tendency
 * Zone 3: Leap - Interleaved skipping
 * Zones 4-7: Meta - All structural params via phi triangle evolution
 *
 * Secret menu: Push+twist encoder to adjust zoneAPhaseOffset
 */
class ScatterZoneA final : public ZoneBasedDualParam<params::GLOBAL_SCATTER_ZONE_A> {
public:
	using ZoneBasedDualParam::ZoneBasedDualParam;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Not relevant for Classic or Burst (gated stutter) modes
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode != ScatterMode::Classic && mode != ScatterMode::Burst;
	}

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "Drift";
		case 1:
			return "Echo";
		case 2:
			return "Fold";
		case 3:
			return "Leap";
		case 4:
			return "Weave";
		case 5:
			return "Spiral";
		case 6:
			return "Bloom";
		case 7:
			return "Void";
		default:
			return "?";
		}
	}

	[[nodiscard]] const char* getShortZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "DR";
		case 1:
			return "EC";
		case 2:
			return "FO";
		case 3:
			return "LP";
		case 4:
			return "WV";
		case 5:
			return "SP";
		case 6:
			return "BL";
		case 7:
			return "VD";
		default:
			return "??";
		}
	}

	// Auto-wrap support: uses per-knob zoneAPhaseOffset
	[[nodiscard]] bool supportsAutoWrap() const override { return true; }
	[[nodiscard]] float getPhaseOffset() const override {
		return soundEditor.currentModControllable->stutterConfig.zoneAPhaseOffset;
	}
	void setPhaseOffset(float offset) override {
		soundEditor.currentModControllable->stutterConfig.zoneAPhaseOffset = offset;
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust zoneAPhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->stutterConfig.zoneAPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 0.1f);
			// Show effective value on display (includes gamma contribution)
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "offset:%d", static_cast<int32_t>(std::floor(effectivePhaseOffset())));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			// Use base class auto-wrap (uses zoneAPhaseOffset via virtual methods)
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

	// Override rendering to show numeric coordinates when phase_offset > 0
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		float phase_offset = effectivePhaseOffset();
		if (phase_offset != 0.0f) {
			cacheCoordDisplay(phase_offset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kScatterResolution, kScatterNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kScatterResolution, kScatterNumZones,
			                           [this](int32_t z) { return this->getZoneName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		float phase_offset = effectivePhaseOffset();
		if (phase_offset != 0.0f) {
			cacheCoordDisplay(phase_offset, this->getValue());
			drawZoneForOled(this->getValue(), kScatterResolution, kScatterNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kScatterResolution, kScatterNumZones,
			                [this](int32_t z) { return this->getZoneName(z); });
		}
	}

private:
	mutable bool suppressNotification_ = false;

	[[nodiscard]] float effectivePhaseOffset() const {
		auto& sc = soundEditor.currentModControllable->stutterConfig;
		return sc.zoneAPhaseOffset + static_cast<float>(kScatterResolution) * sc.gammaPhase;
	}

	static inline char coord_buffer_[12] = {};
	static void cacheCoordDisplay(float phase_offset, int32_t value) {
		int32_t p = static_cast<int32_t>(std::floor(phase_offset));
		int32_t z = value >> 7; // 0-1023 → 0-7 (zone index)
		snprintf(coord_buffer_, sizeof(coord_buffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coord_buffer_; }
};

/**
 * Scatter Zone B - Timbral control
 *
 * Zone 0: Flip - Reverse probability
 * Zone 1: Filter - Bandpass sweep
 * Zone 2: Echo - Delay feedback
 * Zone 3: Shape - Envelope shaping
 * Zones 4-7: Meta - All timbral params via phi triangle evolution
 *
 * Secret menu: Push+twist encoder to adjust zoneBPhaseOffset
 */
class ScatterZoneB final : public ZoneBasedDualParam<params::GLOBAL_SCATTER_ZONE_B> {
public:
	using ZoneBasedDualParam::ZoneBasedDualParam;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Not relevant for Classic or Burst (gated stutter) modes
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode != ScatterMode::Classic && mode != ScatterMode::Burst;
	}

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "Rose";
		case 1:
			return "Blue";
		case 2:
			return "Indigo";
		case 3:
			return "Green";
		case 4:
			return "Lotus";
		case 5:
			return "White";
		case 6:
			return "Grey";
		case 7:
			return "Black";
		default:
			return "?";
		}
	}

	[[nodiscard]] const char* getShortZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "RS";
		case 1:
			return "BL";
		case 2:
			return "IN";
		case 3:
			return "GR";
		case 4:
			return "LO";
		case 5:
			return "WH";
		case 6:
			return "GY";
		case 7:
			return "BK";
		default:
			return "??";
		}
	}

	// Auto-wrap support: uses per-knob zoneBPhaseOffset
	[[nodiscard]] bool supportsAutoWrap() const override { return true; }
	[[nodiscard]] float getPhaseOffset() const override {
		return soundEditor.currentModControllable->stutterConfig.zoneBPhaseOffset;
	}
	void setPhaseOffset(float offset) override {
		soundEditor.currentModControllable->stutterConfig.zoneBPhaseOffset = offset;
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust zoneBPhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->stutterConfig.zoneBPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 0.1f);
			// Show effective value on display (includes gamma contribution)
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "offset:%d", static_cast<int32_t>(std::floor(effectivePhaseOffset())));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			// Use base class auto-wrap (uses zoneBPhaseOffset via virtual methods)
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

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		float phase_offset = effectivePhaseOffset();
		if (phase_offset != 0.0f) {
			cacheCoordDisplay(phase_offset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kScatterResolution, kScatterNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kScatterResolution, kScatterNumZones,
			                           [this](int32_t z) { return this->getZoneName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		float phase_offset = effectivePhaseOffset();
		if (phase_offset != 0.0f) {
			cacheCoordDisplay(phase_offset, this->getValue());
			drawZoneForOled(this->getValue(), kScatterResolution, kScatterNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kScatterResolution, kScatterNumZones,
			                [this](int32_t z) { return this->getZoneName(z); });
		}
	}

private:
	mutable bool suppressNotification_ = false;

	[[nodiscard]] float effectivePhaseOffset() const {
		auto& sc = soundEditor.currentModControllable->stutterConfig;
		return sc.zoneBPhaseOffset + static_cast<float>(kScatterResolution) * sc.gammaPhase;
	}

	static inline char coord_buffer_[12] = {};
	static void cacheCoordDisplay(float phase_offset, int32_t value) {
		int32_t p = static_cast<int32_t>(std::floor(phase_offset));
		int32_t z = value >> 7;
		snprintf(coord_buffer_, sizeof(coord_buffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coord_buffer_; }
};

/**
 * Scatter Macro Config - Configuration for macro parameter behavior
 *
 * Not yet hooked up - placeholder for future macro configuration options
 *
 * Secret menu: Push+twist encoder to adjust macroConfigPhaseOffset
 */
class ScatterMacroConfig final : public ZoneBasedDualParam<params::GLOBAL_SCATTER_MACRO_CONFIG> {
public:
	using ZoneBasedDualParam::ZoneBasedDualParam;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Not relevant for Classic or Burst (gated stutter) modes
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode != ScatterMode::Classic && mode != ScatterMode::Burst;
	}

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		// Abstract weather/nature names (matching automodulator flavor)
		switch (zoneIndex) {
		case 0:
			return "Frost";
		case 1:
			return "Dew";
		case 2:
			return "Fog";
		case 3:
			return "Cloud";
		case 4:
			return "Rain";
		case 5:
			return "Storm";
		case 6:
			return "Dark";
		case 7:
			return "Night";
		default:
			return "?";
		}
	}

	[[nodiscard]] const char* getShortZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "FR";
		case 1:
			return "DW";
		case 2:
			return "FG";
		case 3:
			return "CL";
		case 4:
			return "RN";
		case 5:
			return "ST";
		case 6:
			return "DK";
		case 7:
			return "NT";
		default:
			return "??";
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust macroConfigPhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->stutterConfig.macroConfigPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 0.1f);
			// Show effective value on display (includes gamma contribution)
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "offset:%d", static_cast<int32_t>(std::floor(effectivePhaseOffset())));
			display->displayPopup(buffer);
			renderUIsForOled();
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

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		float phase_offset = effectivePhaseOffset();
		if (phase_offset != 0.0f) {
			cacheCoordDisplay(phase_offset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kScatterResolution, kScatterNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kScatterResolution, kScatterNumZones,
			                           [this](int32_t z) { return this->getZoneName(z); });
		}
	}

	void getColumnLabel(StringBuf& label) override { label.append("mcon"); }

protected:
	void drawPixelsForOled() override {
		float phase_offset = effectivePhaseOffset();
		if (phase_offset != 0.0f) {
			cacheCoordDisplay(phase_offset, this->getValue());
			drawZoneForOled(this->getValue(), kScatterResolution, kScatterNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kScatterResolution, kScatterNumZones,
			                [this](int32_t z) { return this->getZoneName(z); });
		}
	}

private:
	mutable bool suppressNotification_ = false;

	[[nodiscard]] float effectivePhaseOffset() const {
		auto& sc = soundEditor.currentModControllable->stutterConfig;
		return sc.macroConfigPhaseOffset + static_cast<float>(kScatterResolution) * sc.gammaPhase;
	}

	static inline char coord_buffer_[12] = {};
	static void cacheCoordDisplay(float phase_offset, int32_t value) {
		int32_t p = static_cast<int32_t>(std::floor(phase_offset));
		int32_t z = value >> 7;
		snprintf(coord_buffer_, sizeof(coord_buffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coord_buffer_; }
};

} // namespace deluge::gui::menu_item::stutter
