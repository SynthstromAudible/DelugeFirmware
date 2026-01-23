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

#include "definitions_cxx.hpp"
#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/strings.h"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/velocity_encoder.h"
#include "hid/display/display.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item::reverb {

// Resolution and zone count for Featherverb zone params (1024 steps, 8 zones)
static constexpr int32_t kFeatherResolution = 1024;
static constexpr int32_t kFeatherNumZones = 8;

/**
 * Base class for Featherverb zone-based menu items
 * - High-resolution (1024 steps) with velocity-sensitive encoder
 * - 8-zone display with customizable zone names
 * - Only visible when Featherverb is active
 */
class FeatherZoneBase : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	[[nodiscard]] virtual const char* getZoneName(int32_t zoneIndex) const = 0;

	[[nodiscard]] int32_t getMaxValue() const override { return kFeatherResolution - 1; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	// Scale to 0-50 for display (matches gold knob popup range)
	[[nodiscard]] float getDisplayValue() override { return (this->getValue() * 50.0f) / kFeatherResolution; }

	void selectEncoderAction(int32_t offset) override {
		DecimalWithoutScrolling::selectEncoderAction(velocity_.getScaledOffset(offset));
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		renderZoneInHorizontalMenu(slot, this->getValue(), kFeatherResolution, kFeatherNumZones,
		                           [this](int32_t z) { return this->getZoneName(z); });
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::FEATHERVERB;
	}

protected:
	void drawPixelsForOled() override {
		drawZoneForOled(this->getValue(), kFeatherResolution, kFeatherNumZones,
		                [this](int32_t z) { return this->getZoneName(z); });
	}

	mutable VelocityEncoder velocity_;
};

/**
 * Featherverb Zone 1 - Matrix: Controls feedback matrix rotation through orthogonal space
 * 8 zones select different rotation plane combinations via phi triangles
 */
class FeatherZone1 final : public FeatherZoneBase {
public:
	using FeatherZoneBase::FeatherZoneBase;

	void readCurrentValue() override { this->setValue(AudioEngine::reverb.getFeatherZone1()); }
	void writeCurrentValue() override { AudioEngine::reverb.setFeatherZone1(this->getValue()); }

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "L/R"; // L/R swap focused
		case 1:
			return "Front"; // Front blend
		case 2:
			return "Depth"; // Early/late
		case 3:
			return "Space"; // Depth blend
		case 4:
			return "Diffuse"; // Mid diffusion
		case 5:
			return "Stereo"; // Stereo spread
		case 6:
			return "Swirl"; // Complex swirl A
		case 7:
			return "Complex"; // All planes balanced
		default:
			return "?";
		}
	}
};

/**
 * Featherverb Zone 2 - Size: Controls D3 delay length for room size
 * 8 zones from tight room (~29ms) to cathedral (~148ms)
 */
class FeatherZone2 final : public FeatherZoneBase {
public:
	using FeatherZoneBase::FeatherZoneBase;

	void readCurrentValue() override { this->setValue(AudioEngine::reverb.getFeatherZone2()); }
	void writeCurrentValue() override { AudioEngine::reverb.setFeatherZone2(this->getValue()); }

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "Mouse"; // Small room
		case 1:
			return "Rabbit"; // Chamber (compressed)
		case 2:
			return "Lake"; // Concert hall (compressed)
		case 3:
			return "Trees"; // Cathedral (compressed)
		case 4:
			return "Feather"; // Experimental mode placeholder
		case 5:
			return "Sky"; // Nested topology at 2x undersample
		case 6:
			return "Owl"; // Extended tails, FDN+cascade at 4x
		case 7:
			return "Vast"; // Nested topology at 4x undersample
		default:
			return "?";
		}
	}
};

/**
 * Featherverb Zone 3 - Decay: Per-delay feedback character
 * 8 zones with different decay envelopes (balanced, front-heavy, tail-heavy, etc.)
 */
class FeatherZone3 final : public FeatherZoneBase {
public:
	using FeatherZoneBase::FeatherZoneBase;

	void readCurrentValue() override { this->setValue(AudioEngine::reverb.getFeatherZone3()); }
	void writeCurrentValue() override { AudioEngine::reverb.setFeatherZone3(this->getValue()); }

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "Balanced"; // Even decay
		case 1:
			return "Attack"; // Front-heavy
		case 2:
			return "Sustain"; // Tail-heavy
		case 3:
			return "Bounce"; // Alternating
		case 4:
			return "Scoop"; // Mid dip
		case 5:
			return "Hump"; // Mid boost
		case 6:
			return "Sparse"; // Sparse early
		case 7:
			return "Dense"; // Dense early
		default:
			return "?";
		}
	}
};

/**
 * Featherverb Pre-delay - Standard knob (0-50 → 0-100ms)
 * Multi-tap predelay with Zone 2-modulated tap spacing
 * Only visible when Featherverb is active
 * Button press toggles cascade-only mode for A/B testing
 */
class FeatherPredelay final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(AudioEngine::reverb.getFeatherPredelay() * kMaxMenuValue); }
	void writeCurrentValue() override {
		AudioEngine::reverb.setFeatherPredelay(static_cast<float>(this->getValue()) / kMaxMenuValue);
	}

	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::FEATHERVERB;
	}

	/// Click encoder to toggle cascade-only diagnostic mode
	MenuItem* selectButtonPress() override {
		bool current = AudioEngine::reverb.getFeatherCascadeOnly();
		AudioEngine::reverb.setFeatherCascadeOnly(!current);
		display->displayPopup(current ? "FULL" : "CASC");
		return NO_NAVIGATION;
	}
};

} // namespace deluge::gui::menu_item::reverb
