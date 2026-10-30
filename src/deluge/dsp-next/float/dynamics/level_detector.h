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

#include "dsp-next/core/stereo_sample.h"

#include <cstdint>
#include <span>

/// @file
/// @brief LevelDetector: a block-rate, smoothed log level per channel.

namespace deluge::dsp::fp {

/// @brief One level per channel.
struct StereoLevel {
	float l; ///< Left.
	float r; ///< Right.
};

/// @brief A block-rate level detector: per channel, the block's mean absolute sample, smoothed by a one-pole that
/// rises at the attack time and falls at the release time, reported as its natural log.
/// @details A full-scale signal reads about 21 (ln 2^31); the DAC clips near 16. The smoothing weights the current
/// block by its length, so a short block (high CPU load) slows the response less. Used for the reverb's gating and
/// the recorder's level.
class LevelDetector {
public:
	/// @brief The smoothing times.
	struct Settings {
		float attack_ms = 1.0f;   ///< The rise time constant, ms.
		float release_ms = 10.0f; ///< The fall time constant, ms.
	};

	LevelDetector() = default;

	/// @brief A detector with @p settings' smoothing times.
	explicit LevelDetector(Settings settings);

	/// @brief Measures one block.
	/// @param block The block's frames, Q31.
	/// @return ln of each channel's smoothed mean absolute level (in q31 units), never −∞ (a 1e-24 floor).
	[[nodiscard]] StereoLevel measure(std::span<const StereoSample> block);

	/// @brief Returns to silence.
	void reset();

private:
	/// The sample rate the times are set against, Hz.
	static constexpr int32_t kSampleRateHz = 44100;

	/// @brief One smoothing step of @p num_samples from @p current toward @p target, at the attack or release rate.
	[[nodiscard]] float follow(float current, float target, float num_samples) const;

	float attack_ = (-1000.0f / kSampleRateHz) / 1.0f;   ///< Per-sample log decay toward a rising target.
	float release_ = (-1000.0f / kSampleRateHz) / 10.0f; ///< Per-sample log decay toward a falling target.
	float smoothed_l_ = 0;                               ///< The left channel's smoothed mean.
	float smoothed_r_ = 0;                               ///< The right channel's smoothed mean.
};

} // namespace deluge::dsp::fp
