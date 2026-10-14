/// @file
/// @brief The effect chain cases' rig, setup and blocks.
/// @details Each block is one home's chain over the block in place: its runtime (effects::SoundChain, ClipChain or
/// MasterChain) or the same sequence written out over its components (bench/reference_chains.h), at the settings and
/// inputs its owner would hand it, with the owner's delay working state set up first. The rings and tapes come from the
/// app's allocators and delay arena, as an owner's do.

#include "bench/cases.h"

#include "bench/reference_chains.h"
#include "definitions_cxx.hpp"
#include "engines/effects/clip_chain.h"
#include "engines/effects/master_chain.h"
#include "engines/effects/sound_chain.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "processing/render_scratch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>

namespace deluge::bench {

namespace {

/// Unity post-FX and post-reverb volume, Q31.
constexpr int32_t kVolume = 134217728;
/// The playback handler's tick inverse near 120 bpm; the delay is unsynced, so only its start reads it.
constexpr uint32_t kTickInverse = 18'700'000;

/// @brief What every effect chain case renders with: the home's chain, its settings, the work block and the reverb
/// send. The cases run one after another, so they share it; each case's setup makes its chain anew.
struct FxChainRig {
	std::optional<effects::SoundChain> sound;
	std::optional<effects::ClipChain> clip;
	std::optional<effects::MasterChain> master;
	effects::EffectStates settings{};
	dsp::Buffer<StereoSample> work{};                      ///< The lanes output's first n frames, in place.
	std::array<int32_t, SSI_TX_BUFFER_NUM_SAMPLES> send{}; ///< The reverb send, cleared each block.
};

FxChainRig rig{};

/// @return The settings every case's owner holds: the chorus, a digital delay, the compressor's knobs, the default
/// filter modes and some saturation.
effects::EffectStates chain_settings() {
	effects::EffectStates s;
	s.saturation.clipping_amount = 3;
	s.mod_fx.type = ModFXType::CHORUS;
	s.delay = {.ping_pong = true,
	           .type = effects::DelayType::DIGITAL,
	           .sync_type = engine::SyncType::EVEN,
	           .sync_level = engine::SyncLevel::NONE};
	s.compressor = {.attack = 10 << 24,
	                .release = 20 << 24,
	                .threshold = 0x30000000,
	                .ratio = 64 << 24,
	                .sidechain_hpf = 0,
	                .blend = 0x7FFFFFFF};
	return s;
}

/// @brief Gives @p core's delay tapes back, as an owner's teardown does.
void release(effects::ChainCore& core) {
	effects::DelayChange off = core.delay().prepare(effects::DelayType::OFF);
	core.delay().commit(effects::DelayType::OFF, off);
	core.delay().finish(off);
}

/// @brief The hand-overs an owner makes when its settings take effect: the mod FX's ring and the delay's slot.
void hand_over(effects::ChainCore& core) {
	effects::RingChange ring = core.mod_fx().prepare(rig.settings.mod_fx.type);
	core.mod_fx().commit(rig.settings.mod_fx.type, ring);
	core.mod_fx().finish(ring);
	effects::DelayChange delay = core.delay().prepare(rig.settings.delay.type);
	core.delay().commit(rig.settings.delay.type, delay);
	core.delay().finish(delay);
}

/// @return The mod FX, EQ and delay inputs, the delay's working state set up on @p core's delay as an owner sets it
/// up each block.
effects::FxInputs fx_inputs(effects::ChainCore& core, int32_t grain_send) {
	effects::Delay::State delay{};
	delay.delayFeedbackAmount = 0x30000000;
	delay.userDelayRate = 16777216;
	core.delay().setupWorkingState(delay, rig.settings.delay, kTickInverse, true);
	return {
	    .settings = &rig.settings,
	    .mod_fx_type = rig.settings.mod_fx.type,
	    .mod_fx_rate = 0x00800000,
	    .mod_fx_depth = 0x20000000,
	    .mod_fx_offset = 0x08000000,
	    .mod_fx_feedback = 0x10000000,
	    .eq = {.bass = 0x20000000,
	           .treble = -0x10000000,
	           .bass_frequency = 0x10000000,
	           .treble_frequency = -0x10000000},
	    .delay = delay,
	    .stereo = true,
	    .grain_send = grain_send,
	};
}

constexpr effects::BitcrushParams kBitcrush{.bitcrush = -0x40000000, .sample_rate_reduction = -0x40000000};

/// @return The filter params a host chain reads: a low-pass and a high-pass both running.
effects::FilterParams filter_params() {
	return {
	    .lpf_frequency = 0x30000000,
	    .lpf_resonance = 0x10000000,
	    .lpf_morph = 0,
	    .hpf_frequency = 0x04000000,
	    .hpf_resonance = 0x08000000,
	    .hpf_morph = 0,
	    .lpf_frequency_param = 0x30000000,
	    .lpf_morph_param = INT32_MIN,
	    .hpf_frequency_param = -0x40000000,
	    .hpf_morph_param = INT32_MIN,
	    .allow_oversampling = true,
	};
}

/// @brief The rig's chains given back and gone.
void clear() {
	if (rig.sound.has_value()) {
		release(rig.sound->core());
		rig.sound.reset();
	}
	if (rig.clip.has_value()) {
		release(rig.clip->core());
		rig.clip.reset();
	}
	if (rig.master.has_value()) {
		release(rig.master->core());
		rig.master.reset();
	}
}

void sound_block(bool reference) {
	effects::SoundChain& chain = *rig.sound;
	const effects::SoundChainInputs in{
	    .fx = fx_inputs(chain.core(), 0x04000000),
	    .bitcrush = kBitcrush,
	    .post_fx_volume = kVolume,
	    .post_reverb_volume = kVolume,
	    .reverb_send = 0x08000000,
	    .sound = true,
	    .compressor_threshold = 0x40000000,
	    .stutter = nullptr,
	    .stutter_rate = 0,
	};
	const effects::ReverbSend send{rig.send.data(), rig.work.size()};
	if (reference) {
		std::ignore = chain_reference::sound(chain.core(), in, rig.work, send);
	}
	else {
		std::ignore = chain.process(rig.work, in, send);
	}
}

void clip_block(bool reference) {
	effects::ClipChain& chain = *rig.clip;
	const effects::ClipChainInputs in{
	    .fx = fx_inputs(chain.core(), 0x08000000),
	    .bitcrush = kBitcrush,
	    .filter = filter_params(),
	    .filter_scratch = render::filter_scratch(),
	    .volume_post_fx = kVolume,
	    .reverb_send = 0x08000000,
	    .pan = 0x10000000,
	    .sidechain_on = false,
	    .sidechain_hit = 0,
	    .sidechain_shape = 0,
	    .sidechain_depth = 0,
	    .tick_inverse = kTickInverse,
	    .post_reverb_volume_neutral = kVolume,
	    .compressor_threshold = 0x40000000,
	    .stutter = nullptr,
	    .stutter_rate = 0,
	};
	const effects::ReverbSend send{rig.send.data(), rig.work.size()};
	auto tap = [](std::span<const StereoSample> /*block*/) {};
	if (reference) {
		std::ignore =
		    chain_reference::clip(chain.core(), chain.filter(), chain.saturation(), in, rig.work, true, send, tap);
	}
	else {
		effects::ClipBlock levels = chain.begin_block(in, rig.work.size());
		std::ignore = chain.process(rig.work, in, levels, true, send, tap);
	}
}

void master_block(bool reference) {
	effects::MasterChain& chain = *rig.master;
	const effects::MasterChainInputs in{
	    .fx = fx_inputs(chain.core(), 0x02000000),
	    .volume_post_fx = kVolume,
	    .post_reverb_volume = kVolume,
	    .reverb_send = 0x08000000,
	};
	const effects::MasterPostInputs post{
	    .filter = filter_params(),
	    .filter_scratch = render::filter_scratch(),
	    .bitcrush = kBitcrush,
	    .master_volume = 167763968,
	    .do_pan = false,
	    .pan_l = 0,
	    .pan_r = 0,
	    .song_volume = 0x40000000,
	    .stutter = nullptr,
	    .stutter_rate = 0,
	};
	const effects::ReverbSend send{rig.send.data(), rig.work.size()};
	auto between = [&](bool /*sent*/) -> std::optional<effects::MasterPostInputs> { return post; };
	if (reference) {
		std::ignore = chain_reference::master(chain.core(), chain.filter(), in, rig.work, send, between);
	}
	else {
		std::ignore = chain.render(rig.work, in, send, between);
	}
}

} // namespace

void FxChainCase::setup(std::size_t n) {
	clear();
	rig.settings = chain_settings();
	switch (config_.home) {
	case FxChainHome::SOUND:
		rig.sound.emplace(model::kModFxRingAllocator, model::kGranularAllocator, model::delayArena());
		hand_over(rig.sound->core());
		break;
	case FxChainHome::CLIP:
		rig.clip.emplace(model::kModFxRingAllocator, model::kGranularAllocator, model::delayArena(), kVolume);
		hand_over(rig.clip->core());
		break;
	case FxChainHome::MASTER:
		rig.master.emplace(model::kModFxRingAllocator, model::kGranularAllocator, model::delayArena());
		hand_over(rig.master->core());
		break;
	}
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	std::ranges::copy(spans.in, rig.work.begin());
}

void FxChainCase::block(std::size_t /*n*/) {
	std::ranges::fill(rig.send, 0);
	switch (config_.home) {
	case FxChainHome::SOUND:
		sound_block(config_.reference);
		break;
	case FxChainHome::CLIP:
		clip_block(config_.reference);
		break;
	case FxChainHome::MASTER:
		master_block(config_.reference);
		break;
	}
}

} // namespace deluge::bench
