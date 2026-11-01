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
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/reverb/freeverb.h"
#include "dsp-next/float/reverb/plate.h"
#include "effects/param_fragment.h"
#include "effects/sidechain.h"
#include "effects/state.h"
#include "effects/words.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace deluge::effects {

/// @brief The duck the reverb's return takes: the depth and shape in effect, the song's own or those of the owner with
/// the most reverb, as the engine chooses them.
struct ReturnDuck {
	int32_t depth{0}; ///< The sidechain volume as a patch cable's depth, Q29; 0 leaves the envelope still.
	int32_t shape{0}; ///< The shape param, Q31.
};

/// @brief The song's reverb: Freeverb or the plate (Mutable or Digital) over one buffer of the larger's size, the model
/// chosen per block.
/// @details A model change runs in three steps around two audio-excluded sections, so the render never reads memory
/// being zeroed: @ref begin_change (inside the first) stops the render adding any reverb; @ref prepare (between them)
/// zeroes the buffer and resets the model's State, configured from the held settings; @ref commit (inside the second)
/// hands the model to the render. On the interaction tier prepare runs with the render live but adding nothing; on the
/// audio tier (a song swap at a launch) all three run inside the render, where the sections are empty. The owner keeps
/// an audio-tier change from running inside an interaction-tier one (@ref changing). Every change rebuilds, the same
/// model included, as a song load does. The knobs reach the render through @ref publish, which the render takes up at
/// its next block.
class Reverb {
public:
	/// @brief An owner's reverb send param. Neutral: 2^27, Q31 send; a clip's owner builds it around its own amount.
	static constexpr EffectParam kSend{
	    .name = "reverbAmount", .curve = dsp::fixed::ParamCurve::SQUARE, .neutral = 134217728};

	/// @brief The knobs as the song holds them, each 0 to 1.
	struct Settings {
		float room_size = 0.5f; ///< Freeverb's initial room, the component's before any song sets it.
		float damping = 0.5f;
		float width = 1.f;
		float hpf = 0.f;
		float lpf = 0.f;
		bool operator==(const Settings&) const = default;
	};

	/// @brief Freeverb over zeroed memory at the default settings, as the engine starts before any song sets it.
	Reverb();
	Reverb(const Reverb&) = delete;
	Reverb& operator=(const Reverb&) = delete;

	/// @brief Inside the owner's AudioExcludedSection: the render adds no reverb from its next block until commit.
	void begin_change();
	/// @brief After begin_change: zeroes the buffer and resets @p model's State, configured from the settings last
	/// published, and leaves the settings for the render to configure from again at its first block. As ModFx's
	/// prepare, it is the step that runs outside the excluded sections; here it runs between the two.
	/// @param model The model the render takes up at commit.
	void prepare(ReverbModel model);
	/// @brief Inside the owner's AudioExcludedSection: the render renders the prepared model from its next block.
	void commit();
	/// @brief Inside the owner's AudioExcludedSection: @p settings, which the render configures from at its next block.
	/// @param settings The knobs.
	void publish(const Settings& settings);
	/// @brief The model last prepared: the menus' and the owner's.
	/// @return The model.
	[[nodiscard]] ReverbModel model() const { return prepared_; }
	/// @brief The knobs last published.
	/// @return The settings.
	[[nodiscard]] const Settings& settings() const { return settings_; }
	/// @return Whether a model change is between begin_change and commit.
	[[nodiscard]] bool changing() const { return rebuilding_; }
	/// @brief From the render, while a change is under way on the interaction tier: @p model is the one that change
	/// should end at. The changing tier takes it up (@ref take_request) before it commits.
	void request(ReverbModel model) { requested_ = model; }
	/// @brief Inside the changing tier's excluded section: the model the render asked for since the last call, if any,
	/// cleared. The changing tier prepares again at it instead of committing.
	[[nodiscard]] std::optional<ReverbModel> take_request() { return std::exchange(requested_, std::nullopt); }

	/// @brief Render: adds the reverb of @p send to @p block in place at the two return levels, unless a model change
	/// is under way.
	/// @param block The master block, Q31 frames, added to in place.
	/// @param send The reverb send, one sample per frame, Q31.
	/// @param pan_l The left return level, Q31.
	/// @param pan_r The right return level, Q31.
	void render(std::span<dsp::StereoSample> block, std::span<const int32_t> send, int32_t pan_l, int32_t pan_r);

	/// @brief Sets the duck the return takes from the next block.
	/// @param duck The depth and shape in effect.
	void set_return_duck(const ReturnDuck& duck) { return_duck_ = duck; }

	/// @brief Whether the return's duck is in effect (a non-zero depth): only then does @ref return_level read its
	/// rates and tick inverse.
	[[nodiscard]] bool return_ducks() const { return return_duck_.depth != 0; }

	/// @brief The return's level for a block of @p frames: while the depth is non-zero the duck takes @p hit and
	/// advances, whether or not the reverb then renders; at depth 0 it holds still and the level is 2^29.
	/// @param frames The block's length.
	/// @param hit The hit pending for this block, Q31; 0 for none.
	/// @param state The attack, release and sync in effect, read at every block so a menu's edit takes effect at the
	/// next.
	/// @param tick_inverse The playback handler's getTimePerInternalTickInverse(); read only while @ref return_ducks.
	/// @return The return's level, 2^29 unducked.
	[[nodiscard]] int32_t return_level(uint32_t frames, int32_t hit, const SidechainState& state,
	                                   uint32_t tick_inverse);

	/// @brief The return's sidechain.
	[[nodiscard]] Sidechain& sidechain() { return return_sidechain_; }

	/// @brief The models' states and memories, for specs: only the held model's memory may be read.
	[[nodiscard]] const dsp::fixed::Freeverb::State& freeverb_state() const { return freeverb_.state(); }
	[[nodiscard]] const dsp::fp::Plate::State& plate_state() const { return plate_.state(); }
	[[nodiscard]] std::span<const int32_t> freeverb_memory() const { return memory_.freeverb; }
	[[nodiscard]] std::span<const float> plate_memory() const { return memory_.plate; }

private:
	using Freeverb = dsp::fixed::Freeverb;
	using Plate = dsp::fp::Plate;
	/// @brief One buffer for whichever model is held: Freeverb's 25,450 words or the plate's 32,768-float ring.
	union Memory {
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): the kernels index it per frame
		int32_t freeverb[Freeverb::kMemoryWords];
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): as freeverb
		float plate[Plate::kMemoryFloats];
	};
	/// @brief Configures the prepared model from the settings last published. At render time prepared_ is model_: they
	/// differ only between prepare and commit, while rebuilding_ holds the render off.
	void configure();
	/// @brief Zeroes the buffer as Freeverb's lines, making them the union's active member.
	void zero_freeverb();
	/// @brief Zeroes the buffer as the plate's ring, making it the union's active member.
	void zero_plate();

	Memory memory_;
	dsp::Mono<Freeverb> freeverb_;
	dsp::Mono<Plate> plate_;
	ReverbModel model_ = ReverbModel::FREEVERB;    ///< The render's.
	ReverbModel prepared_ = ReverbModel::FREEVERB; ///< The changing tier's, until commit hands it to the render.
	/// Set by begin_change and cleared by commit, both inside an excluded section, so the render reads it with no
	/// writer running; while set, the render reads nothing else.
	bool rebuilding_ = false;
	Settings settings_{};         ///< The knobs last published.
	bool settings_dirty_ = false; ///< Published since the render last configured.
	/// The render's model request during an interaction-tier change: written by the render, read and cleared by the
	/// changing tier inside an excluded section.
	std::optional<ReverbModel> requested_{};
	Sidechain return_sidechain_{}; ///< The return's duck envelope.
	ReturnDuck return_duck_{};     ///< The depth and shape the return's duck takes.
};

static_assert(!std::is_polymorphic_v<Reverb>);

} // namespace deluge::effects
