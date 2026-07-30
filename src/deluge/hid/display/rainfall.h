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

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::hid::display {

/// @brief A field of falling diagonal rain streaks, in the visual grammar of the Deluge logo.
///
/// Each drop is a rigid streak of three or four square cells stepping down-right at 45 degrees,
/// exactly as the streaks in the bootloader's logo do. Nothing ever draws the logo itself.
///
/// @note Deliberately has no display dependency, so the motion maths is unit-testable on the host
///       and the ASCII preview harness can drive it. That is why the panel dimensions below are
///       repeated rather than included -- cpu_specific.h is target-only.
class Rainfall {
public:
	/// @name Panel geometry
	/// @brief Mirrors OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS and OLED_MAIN_TOPMOST_PIXEL.
	///        screensaver.cpp static_asserts these against the real macros, so they cannot drift.
	/// @{
	static constexpr int32_t kWidth = 128;
	static constexpr int32_t kHeight = 48;
	static constexpr int32_t kTopmost = 5;
	static constexpr int32_t kVisibleHeight = kHeight - kTopmost;
	/// @}

	/// @name Tuning
	/// @{

	/// Drops in the field at once.
	static constexpr size_t kNumDrops = 34;
	/// Speed of the most distant drops, px per frame, applied to both axes. On the kSpeedStep grid.
	static constexpr float kSpeedFar = 0.5f;
	/// Speed of the nearest drops, px per frame, applied to both axes. On the kSpeedStep grid.
	static constexpr float kSpeedNear = 1.5f;

	/// @brief Granularity every drop's speed is snapped to, px per frame.
	///
	/// @note Must stay a negative power of two, so that speeds and positions are exactly
	///       representable and a drop's step cadence cannot drift. See roll() for why a coarse,
	///       small-denominator speed is what keeps the motion smooth rather than juddery.
	static constexpr float kSpeedStep = 0.25f;

	/// @brief Chance that a respawning drop enters through the top edge rather than the left.
	///
	/// The ratio of the two edge lengths, which is what makes the flux uniform across the panel.
	/// Spawning only along the top starves the lower-right: everything drifts right, so a drop
	/// seeded past x = kWidth - kVisibleHeight leaves the right edge before it reaches the bottom.
	static constexpr float kSpawnFromTop = static_cast<float>(kWidth) / static_cast<float>(kWidth + kVisibleHeight);

	/// Candidate positions tried per spawn before settling for the roomiest one found.
	static constexpr int32_t kSpawnAttempts = 6;

	/// @name Logo emission
	/// @{

	/// Cells in the logo's grid, and the width and height of that grid in cells.
	static constexpr size_t kLogoCells = 25;
	static constexpr int32_t kLogoGridWidth = 11;
	static constexpr int32_t kLogoGridHeight = 10;

	/// @brief Shortest and longest wait between logo emissions, in frames.
	///
	/// @note 2 to 6 minutes at the screensaver's 50ms tick. Deliberately long: a sighting should
	///       be a surprise, so a short idle period usually shows none at all.
	/// @{
	static constexpr int32_t kLogoIntervalMinFrames = 2400;
	static constexpr int32_t kLogoIntervalMaxFrames = 7200;
	/// @}

	/// @brief Chance a logo is emitted at the smallest cell scale.
	///
	/// @note Weighted away from scale 1: there the logo is single pixels moving at the far layer's
	///       own speed, so the surrounding drizzle camouflages it and it reads as a dense patch of
	///       rain rather than as the mark. Acceptable as the rare case, not as a third of them.
	static constexpr float kLogoSmallScaleChance = 0.10f;

	/// @}

	/// @brief Minimum distance allowed between two live drops of the same block size, px.
	///
	/// @note Only same-size pairs are checked. A near streak passing over a far one is the
	///       parallax reading correctly, and preventing it would need per-frame work.
	/// @param size Block size in pixels: 1, 2 or 3.
	/// @return The minimum permitted distance in pixels.
	[[nodiscard]] static constexpr float minGapFor(int32_t size) {
		return (size == 3) ? 5.0f : ((size == 2) ? 4.0f : 3.0f);
	}

	/// @}

	/// @brief A single square block of a streak.
	///
	/// @warning Unclipped -- the caller clips to the panel.
	struct Block {
		int32_t x;    ///< left edge, pixels
		int32_t y;    ///< top edge, pixels
		int32_t size; ///< 1, 2 or 3, pixels square
	};

	/// @brief A streak's shape and position, without its motion. Exposed so the spacing rule can
	///        be unit-tested against hand-built cases.
	struct Streak {
		float x;        ///< leading cell, left edge
		float y;        ///< leading cell, top edge
		int32_t size;   ///< 1, 2 or 3
		int32_t length; ///< 3 or 4 cells
	};

	/// @brief Squared distance between two streaks, px squared.
	///
	/// Both are parallel segments at 45 degrees, so this is closed-form: the offset across travel
	/// if the two overlap along travel, otherwise the hypotenuse of the across- and along-gaps.
	///
	/// @note Squared, so the caller compares against a squared minimum and no sqrt is needed.
	/// @param a One streak.
	/// @param b The other streak.
	/// @return The squared distance between them, in pixels squared.
	[[nodiscard]] static float separationSquared(const Streak& a, const Streak& b);

	/// @brief Construct a field that is already populated, so the first rendered frame is not
	///        empty.
	Rainfall() { scatter(); }

	/// @brief Re-seed every drop across the panel.
	///
	/// @note Does *not* reseed the LCG, so successive calls produce different fields.
	void scatter();

	/// @brief Advance every drop one frame, respawning any whose tail has left the panel.
	void advance();

	/// @brief Cells in a drop's streak.
	///
	/// @param drop Drop index, less than kNumDrops.
	/// @return 3 or 4, matching the streak lengths in the bootloader's logo.
	[[nodiscard]] size_t lengthOf(size_t drop) const { return drops_[drop].length; }

	/// @brief One block of a drop's streak.
	///
	/// @param drop Drop index, less than kNumDrops.
	/// @param cell Cell index, less than lengthOf(drop). Cell 0 leads, at the bottom-right.
	/// @return The block's unclipped position and pixel size.
	[[nodiscard]] Block cellAt(size_t drop, size_t cell) const;

	/// @brief Speed of a drop, px per frame. Exposed for tests.
	///
	/// @param drop Drop index, less than kNumDrops.
	/// @return The drop's speed, always a multiple of kSpeedStep.
	[[nodiscard]] float speedOf(size_t drop) const { return drops_[drop].speed; }

	/// @name Logo emission
	///
	/// Every few minutes one complete Deluge logo falls through the field and leaves. It is one
	/// object rather than seven drops: the bootloader's logo is exactly seven diagonal runs of
	/// three or four cells -- the rain's own vocabulary, arranged -- so its 25 cells sit at fixed
	/// offsets from a single origin.
	/// @{

	/// @return True while a logo is on or approaching the panel.
	[[nodiscard]] bool logoActive() const { return logo_.active; }

	/// @brief One cell of the logo.
	///
	/// @pre logoActive() is true.
	/// @param cell Cell index, less than kLogoCells.
	/// @return The cell's unclipped block position and pixel size.
	[[nodiscard]] Block logoCellAt(size_t cell) const;

	/// @brief Cell scale of the live logo, in pixels. Exposed for tests.
	///
	/// @return 1, 2 or 3; zero when no logo is active.
	[[nodiscard]] int32_t logoScale() const { return logo_.active ? logo_.size : 0; }

	/// @brief Speed of the live logo, px per frame. Exposed for tests.
	///
	/// @return The logo's speed, always a multiple of kSpeedStep; zero when none is active.
	[[nodiscard]] float logoSpeed() const { return logo_.active ? logo_.speed : 0.0f; }

	/// @brief Emit a logo on the next advance(), whatever the countdown says.
	///
	/// @note For the preview harness: the interval is minutes long, so the animation would
	///       otherwise be uninspectable without waiting it out. Does nothing if one is already up.
	void forceLogo() { logoCountdown_ = 0; }

	/// @}

private:
	struct Drop {
		float x{};            ///< leading cell, left edge; the only axis that is integrated
		float y{};            ///< leading cell, top edge; derived as x - axisOffset
		int32_t axisOffset{}; ///< whole pixels, x - y; fixed for the drop's life
		float speed{};        ///< px per frame, applied to both axes
		uint8_t size{};       ///< 1, 2 or 3; zero marks a slot not yet placed
		uint8_t length{};     ///< 3 or 4 cells
	};

	/// @brief Distance a streak's tail trails behind its head, on each axis, in pixels.
	///
	/// @param size   Block size in pixels: 1, 2 or 3.
	/// @param length Streak length in cells: 3 or 4.
	/// @return The tail's trailing distance, in pixels.
	[[nodiscard]] static constexpr float reachOf(int32_t size, int32_t length) {
		return static_cast<float>((length - 1) * size);
	}

	/// @brief Extract a drop's shape and position as a Streak, for spacing checks.
	///
	/// @param drop The drop to convert.
	/// @return The equivalent Streak.
	[[nodiscard]] static Streak streakOf(const Drop& drop) {
		return {.x = drop.x, .y = drop.y, .size = drop.size, .length = drop.length};
	}

	/// @brief Step the linear congruential generator.
	///
	/// @return The new RNG state.
	uint32_t nextRandom();

	/// @brief Step the linear congruential generator, mapped to [0, 1).
	///
	/// @return The next LCG output, mapped to [0, 1).
	float nextRandomFloat();

	/// @brief Give a drop a fresh depth, and the size, speed and length that follow from it.
	///
	/// @param drop The drop to roll and populate in place.
	void roll(Drop& drop);

	/// @brief Position a drop, either scattered across the panel or entering an edge.
	///
	/// @param drop   The drop to position in place.
	/// @param seeded True to place it anywhere on the panel, false to enter the top or left edge.
	void place(Drop& drop, bool seeded);

	/// @brief The whole logo, as one object.
	///
	/// @note Mirrors Drop's motion contract exactly -- y derived from x by a whole-pixel offset,
	///       speed on the kSpeedStep grid -- so it inherits the same no-wiggle and repeating-cadence
	///       behaviour rather than reimplementing it.
	struct Logo {
		float x{};            ///< grid origin, left edge; the only axis that is integrated
		float y{};            ///< grid origin, top edge; derived as x - axisOffset
		int32_t axisOffset{}; ///< whole pixels, x - y; fixed for the emission
		float speed{};        ///< px per frame, native to size, on the kSpeedStep grid
		uint8_t size{};       ///< cell scale in pixels: 1, 2 or 3
		bool active{};
	};

	/// @brief Start a logo falling, picking its scale, speed and entry edge.
	void emitLogo();

	/// @brief Move the live logo, retire it once clear, and run the emission countdown.
	void advanceLogo();

	/// @return A fresh wait before the next emission, in frames.
	int32_t nextLogoInterval();

	/// @brief Replace drops_[index], honouring the minimum-spacing rule.
	///
	/// @param index  Slot in drops_ to replace.
	/// @param seeded Forwarded to place(): true to scatter anywhere, false to enter an edge.
	void spawn(size_t index, bool seeded);

	std::array<Drop, kNumDrops> drops_{};
	Logo logo_{};
	/// Frames until the next emission. Only counts down while no logo is active, so two can never
	/// overlap and the gap is measured between sightings rather than between starts.
	int32_t logoCountdown_{kLogoIntervalMinFrames};
	uint32_t rngState_{0x1EAF7A11};
};

} // namespace deluge::hid::display
