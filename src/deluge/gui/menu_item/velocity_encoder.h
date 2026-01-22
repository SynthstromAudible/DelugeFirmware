/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "gui/menu_item/menu_item.h"
#include "hid/display/oled.h"
#include "processing/engines/audio_engine.h"
#include <cstdint>

namespace deluge::gui::menu_item {

/// Helper for velocity-based encoder acceleration
/// Faster knob movements result in larger step sizes, while slow movements retain fine control
///
/// Usage:
///   class MyMenuItem : public SomeBase {
///       void selectEncoderAction(int32_t offset) override {
///           SomeBase::selectEncoderAction(velocity_.getScaledOffset(offset));
///       }
///   private:
///       mutable VelocityEncoder velocity_;
///   };
class VelocityEncoder {
public:
	/// Calculate scaled offset based on encoder velocity
	/// @param offset Raw encoder offset (+1 or -1 typically)
	/// @return Scaled offset (1x to 8x based on turning speed)
	int32_t getScaledOffset(int32_t offset) {
		uint32_t now = AudioEngine::audioSampleTimer;
		uint32_t elapsed = now - lastTime_;
		lastTime_ = now;

		// Reset velocity on direction change or long pause (~180ms at 44.1kHz)
		bool directionChanged = (offset > 0) != (lastDirection_ > 0);
		if (directionChanged || elapsed > 8000) {
			velocity_ = 1.0f;
		}
		lastDirection_ = offset;

		// Calculate multiplier based on time between encoder events
		// Faster turning = smaller elapsed time = higher multiplier
		// Thresholds tuned for comfortable encoder turning speeds (~50-150ms per click)
		float mult = 1.0f;
		if (elapsed < 1500) { // Very fast (<34ms)
			mult = 12.0f;
		}
		else if (elapsed < 3500) { // Fast (<79ms)
			mult = 6.0f;
		}
		else if (elapsed < 6500) { // Medium (<147ms)
			mult = 3.0f;
		}

		// Smooth the velocity to avoid abrupt changes
		velocity_ = velocity_ * 0.5f + mult * 0.5f;

		return offset * static_cast<int32_t>(velocity_);
	}

private:
	uint32_t lastTime_{0};
	int32_t lastDirection_{0};
	float velocity_{1.0f};
};

/// Backward compatibility alias
using MomentumEncoder = VelocityEncoder;

/// Render a zone-based parameter in horizontal menu
/// Shows zone name (small text) with position bar below
///
/// @tparam GetZoneName Callable that takes int32_t zone index, returns const char*
/// @param slot The horizontal menu slot parameters
/// @param value Current parameter value
/// @param maxValue Maximum parameter value (e.g., 128 or 1024)
/// @param numZones Number of zones to divide the range into
/// @param getZoneName Function to get zone name from index
template <typename GetZoneName>
void renderZoneInHorizontalMenu(const SlotPosition& slot, int32_t value, int32_t maxValue, int32_t numZones,
                                GetZoneName getZoneName) {
	using namespace deluge::hid::display;

	int32_t stepsPerZone = maxValue / numZones;
	int32_t zoneIndex = value / stepsPerZone;
	if (zoneIndex < 0) {
		zoneIndex = 0;
	}
	if (zoneIndex >= numZones) {
		zoneIndex = numZones - 1;
	}
	const char* zoneName = getZoneName(zoneIndex);

	// Draw zone name centered in slot (small font)
	OLED::main.drawStringCentered(zoneName, slot.start_x, slot.start_y, kTextSmallSpacingX, kTextSmallSizeY,
	                              slot.width);

	// Draw position bar below zone name
	constexpr int32_t barWidth = 20;
	constexpr int32_t barHeight = 3;
	int32_t barX = slot.start_x + (slot.width - barWidth) / 2;
	int32_t barY = slot.start_y + kTextSmallSizeY + 2;

	// Calculate position within zone (0.0 to 1.0)
	int32_t zoneStart = zoneIndex * stepsPerZone;
	int32_t zoneEnd = (zoneIndex + 1) * stepsPerZone;
	float posInZone = static_cast<float>(value - zoneStart) / (zoneEnd - zoneStart);
	if (posInZone < 0.0f) {
		posInZone = 0.0f;
	}
	if (posInZone > 1.0f) {
		posInZone = 1.0f;
	}

	// Draw bar outline
	OLED::main.drawRectangle(barX, barY, barX + barWidth - 1, barY + barHeight - 1);
	// Fill based on position
	int32_t fillWidth = static_cast<int32_t>(posInZone * (barWidth - 2));
	if (fillWidth > 0) {
		OLED::main.invertArea(barX + 1, fillWidth, barY + 1, barY + barHeight - 2);
	}
}

/// Render a zone name prominently for standalone OLED display
/// @tparam GetZoneName Callable that takes int32_t zone index, returns const char*
/// @param value Current parameter value
/// @param maxValue Maximum parameter value
/// @param numZones Number of zones
/// @param getZoneName Function to get zone name from index
template <typename GetZoneName>
void drawZoneForOled(int32_t value, int32_t maxValue, int32_t numZones, GetZoneName getZoneName) {
	using namespace deluge::hid::display;

	int32_t stepsPerZone = maxValue / numZones;
	int32_t zoneIndex = value / stepsPerZone;
	if (zoneIndex < 0) {
		zoneIndex = 0;
	}
	if (zoneIndex >= numZones) {
		zoneIndex = numZones - 1;
	}
	const char* zoneName = getZoneName(zoneIndex);

	OLED::main.drawStringCentred(zoneName, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX, kTextHugeSizeY);
}

} // namespace deluge::gui::menu_item
