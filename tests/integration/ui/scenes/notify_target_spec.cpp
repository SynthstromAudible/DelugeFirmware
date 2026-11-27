/// @file
/// @brief A param change's notify, made through a lane target: what the owner, its delay and its clip are told.

#include "effects/words.h"
#include "model/clip/instrument_clip.h"
#include "model/lane_owner.h"
#include "model/mod_controllable/delay_type.h"
#include "model/model_stack.h"
#include "model/project_bridge/lane_target.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "util/structural_edit_probe.h"

#include <limits>
#include <optional>

#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
namespace bridge = deluge::bridge;
namespace fx = deluge::effects;

namespace {

/// @brief The synth fixture's patched param @p id, as a stack in @p memory.
ModelStackWithAutoParam* patched_param(spec::SongFixture& f, void* memory, int32_t id) {
	return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, deluge::model::owner_of(*f.instrument),
	                                                      f.clip, &f.clip->paramManager)
	    ->getPatchedAutoParamFromId(id);
}

/// @brief Makes the synth fixture's clip its instrument's active one: a sound's notify reaches it only then.
void make_clip_active(spec::SongFixture& f) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	(void)f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip), PgmChangeSend::NEVER);
}

} // namespace

// clang-format off
describe notify_target("a param notify through a lane target", ${
	context("the delay", _ {
		it("names the feedback param's role from the target's kind", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = patched_param(f, a, params::GLOBAL_DELAY_FEEDBACK);
			expect(int(deluge::model::delayParamRole(bridge::target_of(*stack, std::nullopt))))
			    .to_equal(int(deluge::model::DelayParamRole::FEEDBACK));
			spec::make_current(nullptr);
		});

		it("lifts an off delay when its feedback is raised", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = patched_param(f, a, params::GLOBAL_DELAY_FEEDBACK);
			const auto effects = deluge::model::effects_owner_of(stack->owner);
			expect(effects.has_value()).to_be_true();
			if (effects) {
				stack->autoParam->setCurrentValueWithNoReversionOrRecording(stack,
				                                                            std::numeric_limits<int32_t>::min());
				expect(int(effects->effects().delay.type)).to_equal(int(fx::DelayType::OFF), "silent and off first");
				stack->autoParam->setCurrentValueWithNoReversionOrRecording(stack,
				                                                            std::numeric_limits<int32_t>::max());
				expect(int(effects->effects().delay.type)).to_equal(int(fx::next_delay_type(fx::DelayType::OFF)));
			}
			spec::make_current(nullptr);
		});
	});

	context("the clip", _ {
		it("has its next event brought forward when a lane gains automation while the clock runs", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = patched_param(f, a, params::LOCAL_LPF_FREQ);
			auto* manager = static_cast<ParamManagerForTimeline*>(stack->paramManager);
			bridge::LaneTarget lane = bridge::target_of(*stack, std::nullopt);
			lane.timeline = nullptr; // an owner's own lane: the clock alone decides
			const uint8_t saved = playbackHandler.playbackState;
			playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
			manager->ticksTilNextEvent = 1000;
			manager->notifyParamModifiedInSomeWay(lane, 0, true, true);
			expect(manager->ticksTilNextEvent).to_equal(0);
			playbackHandler.playbackState = saved;
			spec::make_current(nullptr);
		});

		it("is told through the lane's own clip, when that clip plays automation", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = patched_param(f, a, params::LOCAL_LPF_FREQ);
			auto* manager = static_cast<ParamManagerForTimeline*>(stack->paramManager);
			const bridge::LaneTarget lane = bridge::target_of(*stack, std::nullopt);
			expect(lane.timeline).to_equal(static_cast<TimelineCounter*>(f.clip));
			const uint8_t saved = playbackHandler.playbackState;
			const bool savedActive = f.clip->activeIfNoSolo;
			playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
			f.clip->activeIfNoSolo = false;
			manager->ticksTilNextEvent = 1000;
			manager->notifyParamModifiedInSomeWay(lane, 0, true, true);
			expect(manager->ticksTilNextEvent).to_equal(1000, "a clip not playing is not told");
			f.clip->activeIfNoSolo = true;
			manager->notifyParamModifiedInSomeWay(lane, 0, true, true);
			expect(manager->ticksTilNextEvent).to_equal(0, "a playing clip is told");
			f.clip->activeIfNoSolo = savedActive;
			playbackHandler.playbackState = saved;
			spec::make_current(nullptr);
		});

		it("is left alone while the clock is stopped", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = patched_param(f, a, params::LOCAL_LPF_FREQ);
			auto* manager = static_cast<ParamManagerForTimeline*>(stack->paramManager);
			const uint8_t saved = playbackHandler.playbackState;
			playbackHandler.playbackState = 0;
			manager->ticksTilNextEvent = 1000;
			manager->notifyParamModifiedInSomeWay(bridge::target_of(*stack, std::nullopt), 0, true, true);
			expect(manager->ticksTilNextEvent).to_equal(1000);
			playbackHandler.playbackState = saved;
			spec::make_current(nullptr);
		});
	});

	context("a sound's own params", _ {
		it("asks for the reverb to be refreshed when its unautomated reverb amount changes", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			make_clip_active(f);
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = patched_param(f, a, params::GLOBAL_REVERB_AMOUNT);
			AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
			stack->paramCollection->notifyParamModifiedInSomeWay(bridge::target_of(*stack, std::nullopt),
			                                                     stack->autoParam->getCurrentValue(), false, false,
			                                                     false);
			expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
			AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
			spec::make_current(nullptr);
		});

		it("is notified the same through a target resolved by address", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			make_clip_active(f);
			const deluge::project::ParamAddress address =
			    bridge::addressOf(deluge::project::OwnerId::instrument(*f.instrument->id()), params::Kind::PATCHED,
			                      params::GLOBAL_REVERB_AMOUNT, f.clip->id());
			const std::optional<bridge::LaneTarget> lane = bridge::lane_exact(*f.song, address);
			expect(lane.has_value()).to_be_true();
			if (lane) {
				AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
				lane->collection->notifyParamModifiedInSomeWay(*lane, lane->auto_param->getCurrentValue(), false,
				                                               false, false);
				expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
				AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
			}
			spec::make_current(nullptr);
		});
	});

	context("a sound's smoothing", _ {
		it("hands its smoothing to the param that changed last", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			Sound& sound = static_cast<SoundInstrument&>(*f.instrument); // the synth fixture's instrument
			expect(params::paramNeedsLPF(params::LOCAL_MODULATOR_0_VOLUME, false)).to_be_true();
			expect(params::paramNeedsLPF(params::LOCAL_MODULATOR_1_VOLUME, false)).to_be_true();
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			alignas(ModelStackWithAutoParam) char b[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* first = patched_param(f, a, params::LOCAL_MODULATOR_0_VOLUME);
			ModelStackWithAutoParam* second = patched_param(f, b, params::LOCAL_MODULATOR_1_VOLUME);
			const bool savedSkipping = sound.skippingRendering;
			sound.skippingRendering = false;
			sound.paramLPF.p = PARAM_LPF_OFF;
			sound.notifyValueChangeViaLPF(params::LOCAL_MODULATOR_0_VOLUME, true,
			                              bridge::target_of(*first, std::nullopt), 5, 100, false);
			expect(sound.paramLPF.p).to_equal(static_cast<int32_t>(params::LOCAL_MODULATOR_0_VOLUME));
			sound.notifyValueChangeViaLPF(params::LOCAL_MODULATOR_1_VOLUME, true,
			                              bridge::target_of(*second, std::nullopt), 7, 100, false);
			expect(sound.paramLPF.p).to_equal(static_cast<int32_t>(params::LOCAL_MODULATOR_1_VOLUME), "the second param has it");
			expect(sound.paramLPF.currentValue).to_equal(7, "from the second param's old value");
			sound.paramLPF.p = PARAM_LPF_OFF;
			sound.skippingRendering = savedSkipping;
			spec::make_current(nullptr);
		});
	});

	context("an oscillator's volume", _ {
		it("re-patches its sound when it starts or stops sounding, and only then", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			make_clip_active(f);
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = patched_param(f, a, params::LOCAL_OSC_A_VOLUME);
			stack->autoParam->setCurrentValueWithNoReversionOrRecording(stack, std::numeric_limits<int32_t>::max());
			const bridge::LaneTarget lane = bridge::target_of(*stack, std::nullopt);
			const uint32_t before = deluge::notedStructuralEdits(deluge::StructuralContainer::PATCH_CABLES);
			stack->paramCollection->notifyParamModifiedInSomeWay(lane, 0, false, false, false);
			expect(deluge::notedStructuralEdits(deluge::StructuralContainer::PATCH_CABLES))
			    .to_equal(before, "sounding before and after: no re-patch");
			stack->paramCollection->notifyParamModifiedInSomeWay(lane, std::numeric_limits<int32_t>::min(), false,
			                                                     false, false);
			expect(deluge::notedStructuralEdits(deluge::StructuralContainer::PATCH_CABLES))
			    .to_equal(before + 1, "silent before, sounding now: one re-patch");
			spec::make_current(nullptr);
		});
	});
});
// clang-format on

CPPSPEC_SPEC(notify_target)
