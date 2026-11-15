#include "instruments/deluge/params.h"
#include "support/printers/runtime_slot_printer.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <limits>
#include <string_view>

#include "support/printers/param_id_printer.h"

using namespace deluge::instrument;
using deluge::plugin::ParamId;
using deluge::plugin::ParamScope;

namespace {
struct Pinned {
	uint32_t id;
	std::string_view name;
};
/// Every row, forever: an id never changes its name, and a name never changes its id.
constexpr std::array<Pinned, 82> kPinned{{
    {0, "oscAVolume"},
    {1, "oscBVolume"},
    {2, "volume"},
    {3, "noiseVolume"},
    {4, "modulator1Volume"},
    {5, "modulator2Volume"},
    {6, "waveFold"},
    {7, "modulator1Feedback"},
    {8, "modulator2Feedback"},
    {9, "carrier1Feedback"},
    {10, "carrier2Feedback"},
    {11, "lpfResonance"},
    {12, "hpfResonance"},
    {13, "env1Sustain"},
    {14, "env2Sustain"},
    {15, "env3Sustain"},
    {16, "env4Sustain"},
    {17, "lpfMorph"},
    {18, "hpfMorph"},
    {19, "oscAPhaseWidth"},
    {20, "oscBPhaseWidth"},
    {21, "oscAWavetablePosition"},
    {22, "oscBWavetablePosition"},
    {23, "pan"},
    {24, "lpfFrequency"},
    {25, "pitch"},
    {26, "oscAPitch"},
    {27, "oscBPitch"},
    {28, "modulator1Pitch"},
    {29, "modulator2Pitch"},
    {30, "hpfFrequency"},
    {31, "lfo2Rate"},
    {32, "lfo4Rate"},
    {33, "env1Attack"},
    {34, "env2Attack"},
    {35, "env3Attack"},
    {36, "env4Attack"},
    {37, "env1Decay"},
    {38, "env2Decay"},
    {39, "env3Decay"},
    {40, "env4Decay"},
    {41, "env1Release"},
    {42, "env2Release"},
    {43, "env3Release"},
    {44, "env4Release"},
    {45, "volumePostFX"},
    {46, "volumePostReverbSend"},
    {47, "reverbAmount"},
    {48, "modFXDepth"},
    {49, "delayFeedback"},
    {50, "delayRate"},
    {51, "modFXRate"},
    {52, "lfo1Rate"},
    {53, "lfo3Rate"},
    {54, "arpRate"},
    {90, "stutterRate"},
    {91, "bass"},
    {92, "treble"},
    {93, "bassFreq"},
    {94, "trebleFreq"},
    {95, "sampleRateReduction"},
    {96, "bitcrushAmount"},
    {97, "modFXOffset"},
    {98, "modFXFeedback"},
    {99, "compressorShape"},
    {100, "compressorThreshold"},
    {101, "arpGate"},
    {102, "rhythm"},
    {103, "sequenceLength"},
    {104, "chordPolyphony"},
    {105, "ratchetAmount"},
    {106, "noteProbability"},
    {107, "reverseProbability"},
    {108, "bassProbability"},
    {109, "swapProbability"},
    {110, "glideProbability"},
    {111, "chordProbability"},
    {112, "ratchetProbability"},
    {113, "spreadGate"},
    {114, "spreadOctave"},
    {115, "spreadVelocity"},
    {116, "portamento"},
}};
constexpr std::array<uint32_t, 6> kCentred{23, 25, 26, 27, 28, 29};
namespace plugin = deluge::plugin;

/// The rows written out field for field: the oracle the table's rows must equal, however the table builds them.
constexpr std::array<plugin::ParamInfo, 22> kRecordedEffectRows{{
    {.id = param::kLpfResonance, .file_name = "lpfResonance", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kHpfResonance,
     .file_name = "hpfResonance",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kLpfMorph,
     .file_name = "lpfMorph",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kHpfMorph,
     .file_name = "hpfMorph",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kLpfFrequency,
     .file_name = "lpfFrequency",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kHpfFrequency,
     .file_name = "hpfFrequency",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kReverbAmount,
     .file_name = "reverbAmount",
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kModFXDepth, .file_name = "modFXDepth", .flags = plugin::ParamFlag::SMOOTHED},
    {.id = param::kDelayFeedback,
     .file_name = "delayFeedback",
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::MIDDLE_DETENT | plugin::ParamFlag::SMOOTHED},
    {.id = param::kDelayRate, .file_name = "delayRate", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kModFXRate, .file_name = "modFXRate"},
    {.id = param::kStutterRate, .file_name = "stutterRate"},
    {.id = param::kBass, .file_name = "bass", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kTreble, .file_name = "treble", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kBassFreq, .file_name = "bassFreq"},
    {.id = param::kTrebleFreq, .file_name = "trebleFreq"},
    {.id = param::kSampleRateReduction,
     .file_name = "sampleRateReduction",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kBitcrushAmount, .file_name = "bitcrushAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModFXOffset, .file_name = "modFXOffset"},
    {.id = param::kModFXFeedback, .file_name = "modFXFeedback"},
    {.id = param::kCompressorShape, .file_name = "compressorShape", .default_value = -601295438},
    {.id = param::kCompressorThreshold, .file_name = "compressorThreshold", .mapping = plugin::MappingKind::UNIPOLAR},
}};

bool same_row(const plugin::ParamInfo& a, const plugin::ParamInfo& b) {
	return a.id == b.id && a.file_name == b.file_name && a.scope == b.scope && a.units == b.units
	       && a.default_value == b.default_value && a.range == b.range && a.mapping == b.mapping && a.flags == b.flags;
}
} // namespace

// clang-format off
describe params("the Deluge instrument's param table", ${
	it("pins every row's id and file name, in id order", _ {
		for (size_t i = 0; i < kPinned.size(); ++i) {
			expect(kParams[i].id.value).to_equal(kPinned[i].id);
			expect(kParams[i].file_name).to_equal(kPinned[i].name);
		}
	});

	it("pins every row's runtime index: patched ids as they are, unpatched ids from 90", _ {
		for (const Pinned& row : kPinned) {
			auto slot = runtime_slot(ParamId{row.id});
			expect(slot).to_have_value();
			if (row.id < kUnpatchedFirstId) {
				expect(static_cast<int>(slot->set)).to_equal(static_cast<int>(RuntimeSet::PATCHED));
				expect(static_cast<uint32_t>(slot->index)).to_equal(row.id);
			}
			else {
				expect(static_cast<int>(slot->set)).to_equal(static_cast<int>(RuntimeSet::UNPATCHED));
				expect(static_cast<uint32_t>(slot->index)).to_equal(row.id - 90);
			}
			expect(param_id(*slot).value).to_equal(row.id);
		}
		expect(runtime_slot(ParamId{55})).not_().to_have_value();
		expect(runtime_slot(ParamId{89})).not_().to_have_value();
		expect(runtime_slot(ParamId{117})).not_().to_have_value();
	});

	it("names each constant for its row", _ {
		expect(param::kOscAVolume.value).to_equal(0u);
		expect(param::kPan.value).to_equal(23u);
		expect(param::kVolumePostFX.value).to_equal(45u);
		expect(param::kArpRate.value).to_equal(54u);
		expect(param::kStutterRate.value).to_equal(90u);
		expect(param::kArpGate.value).to_equal(101u);
		expect(param::kPortamento.value).to_equal(116u);
	});

	it("scopes the voice params per voice and the rest per sound", _ {
		for (const auto& row : kParams) {
			bool perVoice = row.id.value <= 44;
			expect(static_cast<int>(row.scope))
			    .to_equal(static_cast<int>(perVoice ? ParamScope::PER_VOICE : ParamScope::PER_SOUND));
		}
	});

	it("marks pan and the pitches centred, and nothing else", _ {
		for (const auto& row : kParams) {
			bool centred = std::ranges::find(kCentred, row.id.value) != kCentred.end();
			expect(row.flags.has(deluge::plugin::ParamFlag::CENTRED)).to_equal(centred);
		}
	});

	it("stores its ids in two blocks that hold every row and nothing else", _ {
		expect(kParamBlocks[0].first.value).to_equal(0u);
		expect(kParamBlocks[0].count).to_equal(55u);
		expect(kParamBlocks[1].first.value).to_equal(90u);
		expect(kParamBlocks[1].count).to_equal(27u);
		for (const auto& row : kParams) {
			expect(row.id).to_satisfy([](ParamId id) { return kParamBlocks[0].contains(id) || kParamBlocks[1].contains(id); });
		}
	});

	it("starts a new instance with its params' defaults", _ {
		expect(deluge::plugin::find_param(kParams, param::kOscAVolume)->default_value)
		    .to_equal(std::numeric_limits<int32_t>::max());
		expect(deluge::plugin::find_param(kParams, param::kPortamento)->default_value)
		    .to_equal(std::numeric_limits<int32_t>::min());
		expect(deluge::plugin::find_param(kParams, param::kPan)->default_value).to_equal(0);
	});

	it("has one row per name", _ {
		for (const auto& row : kParams) {
			expect(deluge::plugin::find_param(kParams, row.file_name)).to_equal(&row);
		}
	});

	it("keeps every effect row's fields as recorded", _ {
		for (const auto& recorded : kRecordedEffectRows) {
			const auto* row = deluge::plugin::find_param(kParams, recorded.id);
			expect(row != nullptr).to_equal(true);
			expect(same_row(*row, recorded)).to_equal(true);
		}
	});

	it("names each unpatched effect's params by their ids, in the effect's order", _ {
		expect(kEqIds[0].value).to_equal(91U);
		expect(kEqIds[3].value).to_equal(94U);
		expect(kBitcrushIds[0].value).to_equal(96U);
		expect(kBitcrushIds[1].value).to_equal(95U);
	});
});

CPPSPEC_SPEC(params)
