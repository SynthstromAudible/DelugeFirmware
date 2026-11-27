#include "instruments/classic/params.h"
#include "plugin/types/plugin_id.h"
#include "support/printers/runtime_slot_printer.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "support/printers/param_id_printer.h"

using namespace deluge::instruments::classic;
using deluge::plugin::ParamId;
using deluge::plugin::ParamScope;

namespace {
struct Pinned {
	uint32_t id;
	std::string_view name;
};
/// Every row, forever: an id never changes its name, and a name never changes its id.
constexpr std::array<Pinned, 67> kPinned{{
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
    {101, "portamento"},
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

/// Every field of @p row as text, so a failing comparison names the row and shows the field that differs.
std::string row_text(const plugin::ParamInfo& row) {
	return std::format("{} {} scope {} units {} default {} range [{}, {}] mapping {} flags {:#x}", row.id.value,
	                   row.file_name, static_cast<int>(row.scope), static_cast<int>(row.units), row.default_value,
	                   row.range.min, row.range.max, static_cast<int>(row.mapping), row.flags.bits);
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
		expect(param::kPortamento.value).to_equal(101u);
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
		expect(kParamBlocks[1].count).to_equal(12u);
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
			expect(row == nullptr ? std::string{"no row "} + row_text(recorded) : row_text(*row))
			    .to_equal(row_text(recorded));
		}
	});

	it("names each unpatched effect's params by their ids, in the effect's order", _ {
		expect(kEqIds[0].value).to_equal(91U);
		expect(kEqIds[3].value).to_equal(94U);
		expect(kBitcrushIds[0].value).to_equal(96U);
		expect(kBitcrushIds[1].value).to_equal(95U);
	});

	it("finds a param in the table of the plugin that owns it, and in no other", _ {
		// An id each table has and the other lacks, found by looking in the tables themselves.
		const auto only_in = [](std::span<const deluge::plugin::ParamInfo> in, std::span<const deluge::plugin::ParamInfo> not_in) {
			const auto at = std::ranges::find_if(in, [&](const deluge::plugin::ParamInfo& p) {
				return deluge::plugin::find_param(not_in, p.id) == nullptr;
			});
			return at == in.end() ? std::optional<deluge::plugin::ParamId>{} : std::optional{at->id};
		};
		const std::optional<deluge::plugin::ParamId> sound_only = only_in(kParams, deluge::host::chains::kParams);
		const std::optional<deluge::plugin::ParamId> global_only = only_in(deluge::host::chains::kParams, kParams);
		expect(sound_only.has_value()).to_be_true();
		expect(global_only.has_value()).to_be_true();
		if (!sound_only || !global_only) {
			return;
		}
		expect(has_engine_param(deluge::plugin::kDelugeInstrument, *sound_only)).to_be_true();
		expect(has_engine_param(deluge::plugin::kGlobalEffectable, *global_only)).to_be_true();
		expect(has_engine_param(deluge::plugin::kGlobalEffectable, *sound_only)).to_be_false();
		expect(has_engine_param(deluge::plugin::kDelugeInstrument, *global_only)).to_be_false();
		expect(has_engine_param(deluge::plugin::kDelugeInstrument, deluge::plugin::ParamId{4000})).to_be_false();
		expect(has_engine_param(deluge::plugin::kGlobalEffectable, deluge::plugin::ParamId{4000})).to_be_false();
	});
});

CPPSPEC_SPEC(params)
