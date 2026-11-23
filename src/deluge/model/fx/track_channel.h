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

#include "base/function_ref.h"
#include "definitions_cxx.hpp"
#include "dsp/core/stereo_sample.h"
#include "effects/track_chain.h"
#include "model/fx/chain_conversions.h"
#include "model/fx/chain_view.h"
#include "model/fx/effects_binding.h"
#include "model/fx/effects_owner.h"
#include "model/fx/knob_carrier.h"
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

/// @brief A channel's volume and pitch adjust for one block, and the reverb send scaling its content renders with,
/// each read from the params the first time it is asked for.
/// @details An idle track's content renders nothing, and reading its params and their remembered conversions (cold
/// memory) every block was most of what skipping it cost.
class RenderAdjustments {
public:
	/// @param paramManager The params the volume and pitch adjust are read from.
	/// @param conversions The channel's remembered conversions.
	/// @param reverb_amount_adjust The song's reverb send scaling for this output, Q27 (2^27 is 1).
	/// @param reverb_follows_volume Whether the content's reverb send is scaled by the channel's volume.
	RenderAdjustments(ParamManager& paramManager, ChainConversions& conversions, int32_t reverb_amount_adjust,
	                  bool reverb_follows_volume)
	    : param_manager_(paramManager), conversions_(conversions), reverb_amount_adjust_(reverb_amount_adjust),
	      reverb_follows_volume_(reverb_follows_volume) {}

	/// @return The channel's volume adjustment (volume_from_knob), Q27.
	[[nodiscard]] dsp::Q<27> volume();
	/// @return The channel's pitch adjust (pitch_adjust_from_knob).
	[[nodiscard]] dsp::Q<24> pitch();
	/// @return The reverb send scaling the content renders with, Q27: the song's, times the channel's volume when the
	/// reverb follows it.
	[[nodiscard]] int32_t reverbAmount();

private:
	ParamManager& param_manager_;
	ChainConversions& conversions_;
	int32_t reverb_amount_adjust_;
	bool reverb_follows_volume_;
	bool have_volume_ = false;
	bool have_pitch_ = false;
	dsp::Q<27> volume_{};
	dsp::Q<24> pitch_{};
};

/// @brief What a channel hands the track's content for one block.
struct ContentBlock {
	std::span<dsp::StereoSample> buffer{};      ///< The channel's buffer, zeroed: the content renders into it.
	deluge::dsp::Q<31>* reverb_buffer{nullptr}; ///< The song's reverb send buffer.
	int32_t side_chain_hit_pending{};           ///< The sidechain hit pending this block, Q31; 0 for none.
	bool should_limit_delay_feedback{};         ///< Whether a delay's feedback is capped below self-oscillation.
	bool is_clip_active{};                      ///< Whether the track's active clip plays.
	RenderAdjustments* adjustments{nullptr};    ///< The volume, pitch adjust and reverb scaling, read on first use.
};

/// @brief A track's content render: returns whether anything rendered.
using Content = base::function_ref<bool(const ContentBlock&)>;

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
	/// @return The learned knobs held outside a song's table.
	[[nodiscard]] KnobCarrier& knobs() { return knobs_; }
	[[nodiscard]] const KnobCarrier& knobs() const { return knobs_; }
	/// @return The channel's effects, as the functions over an effects owner take them.
	[[nodiscard]] EffectsOwner asEffectsOwner() {
		return EffectsOwner{.core = &core(), .binding = &binding_, .view = &view_, .knobs = &knobs_};
	}

	/// @brief Takes @p other's filters, saturation, mod FX, sidechain, delay, stutter, view and held knobs, as a cloned
	/// audio track does; this channel's compressor stays its own.
	/// @param other The channel cloned from.
	void cloneFrom(const TrackChannel& other);

	/// @brief Renders one block: @p content, then the chain over it, added into @p output, then the clip's param
	/// automation ticks. Once the content has gone silent and the chain's tails have died away, the chain is skipped
	/// until the content renders again. Unlike usual, @p modelStack's timeline counter may be null.
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

	/// @brief The block's adjustments for content rendered outside the chain (a drum stem exported without the kit's
	/// FX): the pitch adjust as usual, the reverb send not scaled by the channel's volume.
	/// @param paramManager The active clip's params (or the track's, with no clip).
	/// @param reverb_amount_adjust The song's reverb send scaling for this output, Q27.
	[[nodiscard]] RenderAdjustments bypassAdjustments(ParamManager& paramManager, int32_t reverb_amount_adjust) {
		return {paramManager, conversions_, reverb_amount_adjust, false};
	}

	/// Whether the track's content rendered last block: the delay starts or stops on it.
	bool rendered_last_time = false;

private:
	/// @brief The clip's param automation moves on by @p frames while the clock runs and the clip is active.
	void tickParamAutomation(ModelStackWithTimelineCounter* modelStack, ParamManager& paramManager, size_t frames,
	                         const ChannelBlock& block);
	/// @brief How long the mod FX and the compressor keep sounding once their input is silent, in samples; 0 when
	///        neither is on.
	[[nodiscard]] int32_t samplesUntilTailsEnd(ParamManager& paramManager);
	/// @brief Starts skipping the chain once the content rendered nothing, the delay has no repeats left, nothing
	///        stutters or records the track (@p held), and the mod FX and compressor tails have died away.
	void reassessSkipping(ParamManager& paramManager, bool held);
	void startSkipping();
	/// @brief Back from skipping: the mod FX's LFO and the sidechain catch up with the time skipped.
	void stopSkipping(ParamManager& paramManager);

	EffectsBinding binding_;
	ChainView view_;
	KnobCarrier knobs_;
	effects::TrackChain chain_;
	/// The chain's remembered param conversions, render tier only.
	ChainConversions conversions_;
	/// Whether the chain is skipped: only the content renders, and the chain runs again once it renders something.
	bool skipping_ = false;
	/// Whether the compressor has been reset since skipping started: nothing runs it while skipping, so once is enough.
	bool compressor_reset_while_skipping_ = false;
	/// When to start skipping, once the tails have died away; 0 while not waiting.
	uint32_t start_skipping_at_ = 0;
	/// When skipping started, for catching up on stopping.
	uint32_t time_started_skipping_ = 0;
};

} // namespace deluge::model
