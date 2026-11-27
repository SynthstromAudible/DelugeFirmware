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

#include "definitions_cxx.hpp" // kMaxNumVoicesUnison
#include "dsp/core/fixed_point.h"
#include "synth/engines/engine.h"
#include "synth/sources/dx7/dx7_source.h"
#include "synth/sources/dx7/dx_voice_port.h"
#include <array>
#include <cstdint>
#include <span>

namespace deluge::synth {

/// @brief The DX7 engine: one DX7 voice per unison part, rendered into the voice's buffer at the DX7's level, the
/// voice's chain around it. It plays no oscillator slot; at its note-on it marks the voice's slots inactive.
class Dx7Engine final : public Engine {
public:
	Dx7Engine() = default;
	Dx7Engine(const Dx7Engine&) = delete;
	Dx7Engine& operator=(const Dx7Engine&) = delete;
	Dx7Engine(Dx7Engine&&) = delete;
	Dx7Engine& operator=(Dx7Engine&&) = delete;
	/// @brief Returns each part's pooled DX7 voice.
	~Dx7Engine() override;

	/// @brief Plays part @p u's DX7 through @p port, which must outlive this engine.
	void bind(int32_t u, DxVoicePort* port);
	/// @brief Marks the parts that can sound, works out their increments, and starts each one's DX7.
	/// @return False when a part's DX7 cannot start (its pool is empty).
	bool note_on(const EngineNote& note) override;
	/// @brief Each part's increment from the DX7's pitch, the master transpose and tune and the unison detune; a part
	/// too high to render goes inactive.
	void calculate_phase_increments(const EngineNote& note) override;
	/// @brief Each sounding part's DX7 at the DX7's level, ramped across the block, panned across the unison spread.
	/// @return Whether it rendered stereo pairs, and whether every part ended.
	EngineRenderResult render(const EngineBlock& block) override;
	/// @brief Keys up each part's DX7, which releases on its own envelopes.
	void release(std::span<UnisonPart> parts, const ReleaseInfo& info, int32_t priority_rating) override;

private:
	/// @brief One unison part's DX7.
	struct Part {
		DX7Source dx7{nullptr};        ///< The part's pooled DX7 voice, through its port
		dsp::UQ<32> phase_increment{}; ///< The part's pitch
		bool active{};                 ///< The part sounds
	};
	std::array<Part, kMaxNumVoicesUnison> parts_{};
};

} // namespace deluge::synth
