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

#include "definitions_cxx.hpp"
#include "dsp-next/core/stereo_sample.h"
#include "effects/track_chain.h"
#include "foundation/function_ref.h"
#include "model/fx/chain_view.h"
#include "model/fx/effects_binding.h"
#include "model/fx/reverb_candidate.h"
#include <cstdint>
#include <span>

class ModControllable;
class ModelStackWithTimelineCounter;
class ParamManager;
class ParamManagerForTimeline;
class SampleRecorder;

namespace deluge::model {

/// @brief What a track hands its channel for one block, beside its params and output.
struct ChannelBlock {
	deluge::dsp::Q<31>* reverb_buffer{nullptr}; ///< The song's reverb send buffer.
	int32_t reverb_amount_adjust{};             ///< The song's reverb send scaling for this output, Q27 (2^27 is 1).
	int32_t side_chain_hit_pending{};           ///< The sidechain hit pending this block, Q31; 0 for none.
	bool should_limit_delay_feedback{};         ///< Whether the delay's feedback is capped below self-oscillation.
	bool is_clip_active{};                      ///< Whether the track's active clip plays (its automation ticks).
	OutputType output_type{};             ///< KIT or AUDIO: the volume headroom factor and the delay's saturation.
	SampleRecorder* recorder{nullptr};    ///< The recorder taking the track's audio, or null.
	ModControllable* tick_owner{nullptr}; ///< The track, which the param tick's model stack names.
};

/// @brief What a channel hands the track's content for one block.
struct ContentBlock {
	std::span<dsp::StereoSample> buffer{};      ///< The channel's buffer, zeroed: the content renders into it.
	deluge::dsp::Q<31>* reverb_buffer{nullptr}; ///< The song's reverb send buffer.
	int32_t reverb_amount_adjust{};     ///< The reverb send scaling, already scaled by the channel's volume, Q27.
	int32_t side_chain_hit_pending{};   ///< The sidechain hit pending this block, Q31; 0 for none.
	bool should_limit_delay_feedback{}; ///< Whether a delay's feedback is capped below self-oscillation.
	bool is_clip_active{};              ///< Whether the track's active clip plays.
	int32_t pitch_adjust{};             ///< The channel's pitch adjust, as its param gives it.
};

/// @brief A track's content render: returns whether anything rendered.
using Content = function_ref<bool(const ContentBlock&)>;

/// @brief A kit's or audio track's channel strip: its insert chain, the chain's settings and the render's copy of them,
/// and which values its knobs edit.
class TrackChannel {
public:
	/// @brief A chain with the app's allocators, its settings at their defaults and projected.
	TrackChannel();
	/// @brief Gives the delay's tapes back to the arena.
	~TrackChannel();
	TrackChannel(const TrackChannel&) = delete;
	TrackChannel& operator=(const TrackChannel&) = delete;

	/// @return The insert chain.
	[[nodiscard]] effects::TrackChain& chain() { return chain_; }
	/// @return The chain's core: the effect components every chain has.
	[[nodiscard]] effects::ChainCore& core() { return chain_.core(); }
	[[nodiscard]] const effects::ChainCore& core() const { return chain_.core(); }
	/// @return The chain's filters.
	[[nodiscard]] effects::Filter& filter() { return chain_.filter(); }
	/// @return The chain's settings and the render's copy of them.
	[[nodiscard]] EffectsBinding& binding() { return binding_; }
	[[nodiscard]] const EffectsBinding& binding() const { return binding_; }
	/// @return Which values the chain's knobs edit.
	[[nodiscard]] ChainView& view() { return view_; }
	[[nodiscard]] const ChainView& view() const { return view_; }

	/// @brief Renders one block: the chain around @p content, added into @p output, then the clip's param automation
	/// ticks. Unlike usual, @p modelStack's timeline counter may be null.
	/// @param modelStack The track's model stack.
	/// @param paramManager The active clip's params (or the track's, with no clip).
	/// @param output The song's mix, which the channel adds into.
	/// @param block The block's inputs from the track.
	/// @param content The track's content render (drums, an audio clip).
	void render(ModelStackWithTimelineCounter* modelStack, ParamManager& paramManager,
	            std::span<dsp::StereoSample> output, const ChannelBlock& block, Content content);

	/// @brief The reverb's auto duck's search past this channel: its send, when its active clip's params hold one
	/// unautomated and it beats @p best; then how it ducks the return.
	/// @param best The best so far.
	/// @param paramManager The active clip's params.
	/// @return The new best.
	[[nodiscard]] ReverbCandidate reverbCandidate(ReverbCandidate best, ParamManagerForTimeline& paramManager) const;

	/// Whether the track's content rendered last block: the delay starts or stops on it.
	bool rendered_last_time = false;

private:
	EffectsBinding binding_;
	ChainView view_;
	effects::TrackChain chain_;
};

} // namespace deluge::model
