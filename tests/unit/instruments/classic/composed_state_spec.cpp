#include "instruments/classic/composed_state.h"
#include "instruments/classic/dx7_init_voice.h"
#include "instruments/classic/flat_layout.h"
#include "support/golden_update.h"
#include "support/record_reference.h"
#include "support/sound_matrix.h"

#include <cppspec.hpp>
#include <cstddef>
#include <format>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::instruments::classic;

namespace {

/// Whether @p flat survives being composed and flattened again.
bool round_trips(const FlatSoundState& flat) {
	const auto [state, recall] = compose(flat);
	return flatten(state, recall) == flat;
}

/// A sound whose slots hold the given kinds, each with some settings of its own and some of the other kinds'.
/// The recorded reference's sounds and their names: the matrix's engine and kind pairs, and the new sound.
template <class Check>
void for_each_reference_sound(const Check& check) {
	deluge::test::for_each_named_sound([&](const std::string& name, const SoundState& sound) {
		if (deluge::test::is_kind_row(name)) {
			check(name, sound);
		}
	});
}

constexpr const char* kReferenceGolden = "instrument/record_reference.tsv";

/// The reference table: a header naming each family, then one line per sound of the matrix with each answer's hash.
std::string reference_table() {
	std::string table;
	bool header = true;
	for_each_reference_sound([&](const std::string& name, const SoundState& sound) {
		const std::vector<deluge::test::ReferenceAnswer> answers = deluge::test::reference_answers(sound);
		if (header) {
			table += "sound";
			for (const deluge::test::ReferenceAnswer& answer : answers) {
				table += "\t" + answer.family;
			}
			table += "\n";
			header = false;
		}
		table += name;
		for (const deluge::test::ReferenceAnswer& answer : answers) {
			table += "\t" + deluge::test::reference_hash(answer.text);
		}
		table += "\n";
	});
	return table;
}

std::vector<std::string> split(const std::string& line, char separator) {
	std::vector<std::string> fields;
	std::string field;
	std::istringstream in(line);
	while (std::getline(in, field, separator)) {
		fields.push_back(field);
	}
	return fields;
}

/// Where @p got's table and @p want's first differ, as "<sound>: <family>"; empty where they agree.
std::string first_reference_difference(const std::string& got, const std::string& want) {
	const std::vector<std::string> got_lines = split(got, '\n');
	const std::vector<std::string> want_lines = split(want, '\n');
	if (got_lines.empty() || want_lines.empty()) {
		return "an empty table";
	}
	if (got_lines.front() != want_lines.front()) {
		return "the families: " + got_lines.front();
	}
	const std::vector<std::string> families = split(got_lines.front(), '\t');
	std::map<std::string, std::vector<std::string>> recorded;
	for (size_t i = 1; i < want_lines.size(); i++) {
		std::vector<std::string> fields = split(want_lines[i], '\t');
		if (!fields.empty()) {
			recorded[fields.front()] = std::move(fields);
		}
	}
	for (size_t i = 1; i < got_lines.size(); i++) {
		const std::vector<std::string> fields = split(got_lines[i], '\t');
		if (fields.empty()) {
			return "a blank line";
		}
		const auto found = recorded.find(fields.front());
		if (found == recorded.end()) {
			return fields.front() + ": not recorded";
		}
		for (size_t f = 1; f < fields.size(); f++) {
			if (f >= found->second.size() || fields[f] != found->second[f]) {
				return fields.front() + ": " + families[f];
			}
		}
	}
	return got_lines.size() == want_lines.size() ? std::string{} : std::string{"a recorded sound the matrix lacks"};
}

} // namespace

// clang-format off
describe composed_state("a sound's flat record as the state it plays and what it keeps for a switch back", ${
	it("composes a new synth into the components' defaults, with nothing to recall", _ {
		const auto [state, recall] = compose(FlatSoundState{});
		expect(state).to_equal(ComposedState{});
		expect(recall).to_equal(Recall{});
	});

	it("round-trips every engine with every pair of oscillator kinds, the other kinds' settings kept", _ {
		deluge::test::for_each_sound([&](const SoundState& sound) { expect(round_trips(to_flat(sound))).to_equal(true); });
	});

	it("answers every question about every engine with every pair of oscillator kinds as recorded", _ {
		const std::string path = std::string(GOLDEN_ROOT) + "/" + kReferenceGolden;
		const std::string table = reference_table();
		if (deluge::test::golden::updating()) {
			deluge::test::golden::write_if_changed(path, table);
		}
		const std::optional<std::string> recorded = deluge::test::golden::read(path);
		expect(recorded.has_value()).to_equal(true);
		expect(first_reference_difference(table, recorded.value_or(std::string{}))).to_equal(std::string{});
	});

	it("plays an FM sound's tuning and sync on its carriers, and keeps its oscillators' kinds to recall", _ {
		const SoundState sound = deluge::test::plain_sound(SynthMode::FM, OscType::SAMPLE, OscType::SAW);
		const ComposedState& state = sound.plays;
		const Recall& recall = sound.recall;
		const auto& fm = std::get<deluge::synth::FmState>(state.engine);
		expect(int{fm.carriers[0].tuning.transpose}).to_equal(5);
		// The sample oscillator is parked whole, its ranges with it, until FM goes
		expect(recall.slots[0].parked.has_value()).to_equal(true);
		const auto* parked = std::get_if<deluge::synth::SampleState>(&*recall.slots[0].parked);
		expect(parked).not_().to_be_null();
		expect(parked != nullptr ? parked->ranges.size() : size_t{0}).to_equal(size_t{2});
		expect(fm.sync).to_equal(true); // the file writes it in the second carrier's pitch
	});

	it("keeps the sync for a switch back when the second oscillator is a sample, which the file writes it in", _ {
		const SoundState sound = deluge::test::plain_sound(SynthMode::SUBTRACTIVE, OscType::SINE, OscType::SAMPLE);
		const ComposedState& state = sound.plays;
		const Recall& recall = sound.recall;
		expect(std::get<deluge::synth::SubtractiveState>(state.engine).oscillators.sync).to_equal(false);
		expect(recall.sync).to_equal(true);
	});

	it("gives a sample oscillator its ranges' tuning and no pitch of its own", _ {
		const SoundState sound = deluge::test::plain_sound(SynthMode::SUBTRACTIVE, OscType::SAMPLE, OscType::SINE);
		const ComposedState& state = sound.plays;
		const Recall& recall = sound.recall;
		const auto& slot = std::get<deluge::synth::SampleState>(
		    std::get<deluge::synth::SubtractiveState>(state.engine).oscillators.slots[0]);
		expect(slot.ranges.size()).to_equal(size_t{2});
		expect(int{slot.ranges[1].tuning.transpose}).to_equal(2);
		expect(recall.slots[0].sample_pitch.retrigger.has_value()).to_equal(true); // the file does not write a sample's retrigger
		expect(int{recall.slots[0].sample_pitch.tuning.transpose}).to_equal(5);
	});
});
// clang-format on

DELUGE_SPEC(composed_state_spec, composed_state)
