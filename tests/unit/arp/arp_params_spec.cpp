// arp/params.h: one table for every arp, holding today's numbers. The oracles are the tables the arp's rows leave: the
// Deluge instrument's (a sound's arp), the host chain's (a kit's arp) and the arp's host rows (a MIDI or CV clip's arp,
// a MIDI or gate kit row's).
#include "arp/params.h"
#include "arp/values.h"

#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "plugin/types/arp.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "support/spec_main.h"

namespace arp = deluge::arp;
namespace classic = deluge::instruments::classic;
namespace chains = deluge::host::chains;
using deluge::plugin::ParamId;
using deluge::plugin::ParamInfo;

namespace {

/// The instrument's and the host chain's id for each arp row, in the arp's order.
constexpr std::array<ParamId, arp::kParamCount> kClassicIds{
    classic::param::kArpGate,
    classic::param::kArpRate,
    classic::param::kRhythm,
    classic::param::kSequenceLength,
    classic::param::kChordPolyphony,
    classic::param::kRatchetAmount,
    classic::param::kNoteProbability,
    classic::param::kBassProbability,
    classic::param::kSwapProbability,
    classic::param::kGlideProbability,
    classic::param::kReverseProbability,
    classic::param::kChordProbability,
    classic::param::kRatchetProbability,
    classic::param::kSpreadVelocity,
    classic::param::kSpreadGate,
    classic::param::kSpreadOctave,
};
constexpr std::array<ParamId, arp::kParamCount> kChainIds{
    chains::param::kArpGate,
    chains::param::kArpRate,
    chains::param::kRhythm,
    chains::param::kSequenceLength,
    chains::param::kChordPolyphony,
    chains::param::kRatchetAmount,
    chains::param::kNoteProbability,
    chains::param::kBassProbability,
    chains::param::kSwapProbability,
    chains::param::kGlideProbability,
    chains::param::kReverseProbability,
    chains::param::kChordProbability,
    chains::param::kRatchetProbability,
    chains::param::kSpreadVelocity,
    chains::param::kSpreadGate,
    chains::param::kSpreadOctave,
};

const ParamInfo& row_of(std::span<const ParamInfo> table, ParamId id) {
	const ParamInfo* row = deluge::plugin::find_param(table, id);
	return row != nullptr ? *row : arp::kParams[0].info;
}

/// A host row's default as the param word: the fourteen unsigned rows store word + 2^31.
int32_t host_default_word(std::size_t i) {
	const auto value = static_cast<int32_t>(deluge::plugin::kArpHostRows[i].default_value);
	return arp::kParams[i].unsigned_field ? static_cast<int32_t>(static_cast<uint32_t>(value) ^ 0x80000000u) : value;
}

} // namespace

// clang-format off
describe arp_params("arp/params.h", ${
	it("has sixteen dense ids in the host rows' order", _ {
		expect(arp::kParams.size()).to_equal(deluge::plugin::kArpHostRows.size());
		for (std::size_t i = 0; i < arp::kParamCount; ++i) {
			expect(arp::kParams[i].info.id.value).to_equal(static_cast<uint32_t>(i));
			expect(std::string{"arpeggiator/"} + std::string{arp::kParams[i].setting_name})
			    .to_equal(std::string{deluge::plugin::kArpHostRows[i].path});
		}
	});

	it("names, maps and flags each row as the instrument and the host chain did", _ {
		for (std::size_t i = 0; i < arp::kParamCount; ++i) {
			const ParamInfo& mine = arp::kParams[i].info;
			const ParamInfo& sound = row_of(classic::kParams, kClassicIds[i]);
			const ParamInfo& kit = row_of(chains::kParams, kChainIds[i]);
			expect(std::string{mine.file_name}).to_equal(std::string{sound.file_name});
			expect(std::string{mine.file_name}).to_equal(std::string{kit.file_name});
			expect(static_cast<int32_t>(mine.mapping)).to_equal(static_cast<int32_t>(kit.mapping));
			expect(mine.flags == kit.flags).to_equal(true);
		}
	});

	it("starts each owner's arp where that owner's table did", _ {
		for (std::size_t i = 0; i < arp::kParamCount; ++i) {
			const auto id = arp::id_at(i);
			expect(arp::default_value(arp::Owner::SOUND, id)).to_equal(row_of(classic::kParams, kClassicIds[i]).default_value);
			expect(arp::default_value(arp::Owner::KIT, id)).to_equal(row_of(chains::kParams, kChainIds[i]).default_value);
			expect(arp::default_value(arp::Owner::CLIP, id)).to_equal(host_default_word(i));
			expect(arp::default_value(arp::Owner::ROW, id)).to_equal(host_default_word(i));
		}
	});

	it("finds each row by its file name and its setting name", _ {
		for (std::size_t i = 0; i < arp::kParamCount; ++i) {
			expect(arp::find_by_file_name(arp::kParams[i].info.file_name)->value).to_equal(static_cast<uint32_t>(i));
			expect(arp::find_by_setting_name(arp::kParams[i].setting_name)->value).to_equal(static_cast<uint32_t>(i));
		}
		expect(arp::find_by_file_name("portamento").has_value()).to_equal(false);
	});

	it("calls only the rhythm the rhythm", _ {
		for (std::size_t i = 0; i < arp::kParamCount; ++i) {
			expect(arp::is_rhythm(arp::id_at(i))).to_equal(arp::id_at(i) == arp::param::kRhythm);
		}
	});

	it("gives the engine an unsigned field as word + 2^31", _ {
		arp::Values values;
		values.set(arp::param::kRhythm, std::numeric_limits<int32_t>::min());
		values.set(arp::param::kNoteProbability, std::numeric_limits<int32_t>::max());
		expect(values.unsigned_word(arp::param::kRhythm)).to_equal(0u);
		expect(values.unsigned_word(arp::param::kNoteProbability)).to_equal(0xFFFFFFFFu);
		values.set(arp::param::kGate, -5);
		expect(values[arp::param::kGate]).to_equal(-5);
	});
});
// clang-format on

DELUGE_SPEC(arp_params_spec, arp_params)
