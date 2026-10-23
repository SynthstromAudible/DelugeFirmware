// The gold knob's position, from the param's descriptor: the same knob position, value, middle and bipolar answers as
// the engine's own param collections give, for every param of the Deluge's and the global effectable's tables, a
// patch cable, the three expression dimensions and a MIDI CC.

#include "scenes/bind/knob_math.h"

#include "engines/deluge/cables.h"
#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"

#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/set_patch_cable.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using deluge::engine::ParamInfo;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
using deluge::project::ParamSpace;
namespace bind = deluge::scenes::bind;

constexpr std::array<int32_t, 7> kValues{std::numeric_limits<int32_t>::min(), -(1 << 30), -1, 0, 1, 1 << 30,
                                         std::numeric_limits<int32_t>::max()};

/// @brief Where the knob math and the engine first disagree at @p address, or empty.
std::string disagreement(::Song& song, const ParamAddress& address, const ParamInfo* info) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
	if (stack == nullptr || stack->autoParam == nullptr) {
		return "no stack for id " + std::to_string(address.id.value);
	}
	ParamCollection* collection = stack->paramCollection;
	const std::string at = "id " + std::to_string(address.id.value) + ": ";
	for (const int32_t value : kValues) {
		if (collection->paramValueToKnobPos(value, stack) != bind::knob_pos_of(address, info, value)) {
			return at + "knob position of " + std::to_string(value);
		}
	}
	for (int32_t knob = -64; knob <= 64; knob++) {
		if (collection->knobPosToParamValue(knob, stack) != bind::value_of_knob(address, info, knob)) {
			return at + "value of knob " + std::to_string(knob);
		}
	}
	const bool middle = collection->shouldParamIndicateMiddleValue(stack);
	const bool stutter = address.space == ParamSpace::ENGINE && info != nullptr && info->file_name == "stutterRate";
	if (!stutter && middle != bind::indicates_middle(address, info, false, false)) {
		return at + "indicates middle";
	}
	const bool bipolar = deluge::modulation::params::isParamBipolar(collection->getParamKind(), stack->paramId);
	if (bipolar != bind::bipolar(address, info)) {
		return at + "bipolar";
	}
	return {};
}

} // namespace

// clang-format off
describe knob_math("the gold knob's position, from the param's descriptor", ${
	it("agrees with the engine for every Deluge param, a patch cable, expression 0-2 and a MIDI CC", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		const OwnerId synth = OwnerId::instrument(*f.instrument->id());
		std::vector<std::string> found;
		for (const ParamInfo& info : deluge::instrument::kParams) {
			const ParamAddress address{.owner = synth, .space = ParamSpace::ENGINE, .id = info.id, .clip_id = f.clip->id()};
			if (std::string why = disagreement(*f.song, address, &info); !why.empty()) {
				found.push_back(why);
			}
		}
		// A cable: LFO 1 to pitch, made first.
		const std::optional<deluge::engine::ParamId> depth = deluge::instrument::cable_depth_id(
		    deluge::instrument::PatchSource::LFO_GLOBAL_1, deluge::instrument::param::kPitch);
		expect(depth.has_value()).to_be_true();
		if (depth) {
			const ParamAddress cable{.owner = synth, .space = ParamSpace::ENGINE, .id = *depth, .clip_id = f.clip->id()};
			deluge::edits::Context ctx{*f.song, f.song->project};
			deluge::edits::SetPatchCable make{cable, 1 << 28, Polarity::BIPOLAR};
			expect(deluge::edits::apply(ctx, make) == Error::NONE).to_be_true();
			if (std::string why = disagreement(*f.song, cable, nullptr); !why.empty()) {
				found.push_back("cable " + why);
			}
		}
		// The expression dimensions, once the clip has its set.
		(void)f.clip->paramManager.getOrCreateExpressionParamSet();
		for (uint32_t dimension = 0; dimension < 3; dimension++) {
			const ParamAddress expression{
			    .owner = synth, .space = ParamSpace::EXPRESSION, .id = {dimension}, .clip_id = f.clip->id()};
			if (std::string why = disagreement(*f.song, expression, nullptr); !why.empty()) {
				found.push_back("expression " + why);
			}
		}
		for (const std::string& why : found) {
			std::fputs(("KNOB-MATH " + why + "\n").c_str(), stdout);
		}
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("agrees with the engine for every global-effectable param", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		std::vector<std::string> found;
		for (const ParamInfo& info : deluge::global_effectable::kParams) {
			const ParamAddress address{.owner = OwnerId::song(), .space = ParamSpace::ENGINE, .id = info.id};
			if (std::string why = disagreement(*f.song, address, &info); !why.empty()) {
				found.push_back(why);
			}
		}
		for (const std::string& why : found) {
			std::fputs(("KNOB-MATH global " + why + "\n").c_str(), stdout);
		}
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("agrees with the engine for a MIDI CC", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		{
			char memory[MODEL_STACK_MAX_SIZE];
			(void)midi->getParamToControlFromInputMIDIChannel(
			    74, setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, midi, f.clip, &f.clip->paramManager));
		}
		const ParamAddress cc{.owner = OwnerId::clip(f.clip->id()), .space = ParamSpace::MIDI_CC, .id = {74}};
		const std::string why = disagreement(*f.song, cc, nullptr);
		if (!why.empty()) {
			std::fputs(("KNOB-MATH cc " + why + "\n").c_str(), stdout);
		}
		expect(why.empty()).to_be_true();
		currentSong = before;
	});

	it("indicates the middle of a stutter rate unless it is quantized, or while stuttering", _ {
		const ParamInfo* rate =
		    deluge::engine::find_param(deluge::global_effectable::kParams, deluge::global_effectable::param::kStutterRate);
		const ParamAddress address{.owner = OwnerId::song(), .space = ParamSpace::ENGINE, .id = rate->id};
		expect(bind::indicates_middle(address, rate, false, false)).to_be_true();
		expect(bind::indicates_middle(address, rate, true, false)).to_be_false();
		expect(bind::indicates_middle(address, rate, true, true)).to_be_true();
	});

	it("turns as the gold knob does", _ {
		using bind::KnobKind;
		expect(bind::turn(KnobKind::PARAM, 0, 1)).to_equal(1);
		expect(bind::turn(KnobKind::PARAM, 64, 1)).to_equal(64);
		expect(bind::turn(KnobKind::PARAM, -64, -1)).to_equal(-64);
		expect(bind::turn(KnobKind::MIDI_CC, 63, 1)).to_equal(63);
		expect(bind::turn(KnobKind::CABLE, -192, -1)).to_equal(-192);
		expect(bind::turn(KnobKind::CABLE, -100, -1)).to_equal(-101);
		const ParamAddress cc{.space = ParamSpace::MIDI_CC, .id = {74}};
		expect(bind::knob_kind(cc) == KnobKind::MIDI_CC).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(knob_math)
