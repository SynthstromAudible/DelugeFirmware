#include "engine-api/types/setting.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "scenes/bind/setting_context.h"
#include "scenes/bind/slot_reader.h"
#include "support/model_fixture.h"

#include <cppspec.hpp>
#include <string>

using namespace deluge::scenes::bind;
using deluge::engine::SettingContext;
using deluge::engine::SettingKind;
using deluge::scenes::bind::test::kDrum;
using deluge::scenes::bind::test::kSynth;
using deluge::scenes::bind::test::ModelFixture;

namespace {
/// Every row where what SlotReader gives for @p slot differs from held_setting of the sound's typed state, as
/// "path=got/want"; empty when every row agrees.
std::string reads_unlike_state(const ModelFixture& fixture, deluge::project::SlotAddress slot) {
	const SlotReader reader{fixture.song, slot, fixture.facts};
	const deluge::instrument::SoundState& state = fixture.facts.states.at(slot.id);
	std::string unlike;
	for (const deluge::engine::SettingInfo& row : deluge::instrument::kSettings) {
		if (row.kind == SettingKind::STRUCTURED || row.kind == SettingKind::TEXT) {
			continue;
		}
		const auto want = deluge::instrument::held_setting(state, row.path);
		const auto got = reader.read(row.path);
		if (got != want) {
			unlike += std::string(row.path) + "=" + (got ? std::to_string(*got) : "none") + "/"
			          + (want ? std::to_string(*want) : "none") + " ";
		}
	}
	return unlike;
}
} // namespace

// clang-format off
describe bind_reader("a sound's settings read through its slot's address", ${
	it("reads every row a synth track and a kit row hold, as the sound's typed state holds it", _ {
		const ModelFixture fixture;
		expect(reads_unlike_state(fixture, kSynth)).to_equal(std::string{});
		expect(reads_unlike_state(fixture, kDrum)).to_equal(std::string{});
	});

	it("reads a changed state: a choice, a number, an LFO's sync level and an FM sound's oscillator type", _ {
		ModelFixture fixture;
		deluge::instrument::SoundState state = fixture.facts.states.at(kSynth.id);
		state.polyphonic = deluge::instrument::PolyphonyMode::MONO;
		state.num_unison = 4;
		state.lfo_sync_level[0] = deluge::engine::SyncLevel{5};
		state.synth_mode = deluge::instrument::SynthMode::FM;
		state.sources[0].osc_type = deluge::instrument::OscType::SAW;
		fixture.set_state(kSynth, state);
		expect(reads_unlike_state(fixture, kSynth)).to_equal(std::string{});
		const SlotReader reader{fixture.song, kSynth, fixture.facts};
		expect(reader.read("osc1/type").value_or(-1)).to_equal(static_cast<int32_t>(deluge::instrument::OscType::SAW));
	});

	it("reads every row the table holds: the host contract's reader never answers nothing for one", _ {
		const ModelFixture fixture;
		for (const deluge::project::SlotAddress slot : {kSynth, kDrum}) {
			const SlotReader reader{fixture.song, slot, fixture.facts};
			std::string missing;
			for (const deluge::engine::SettingInfo& row : deluge::instrument::kSettings) {
				if (row.kind != SettingKind::STRUCTURED && row.kind != SettingKind::TEXT && !reader.read(row.path)) {
					missing += std::string(row.path) + " ";
				}
			}
			expect(missing).to_equal(std::string{});
		}
	});

	it("reads nothing for a path the table does not hold", _ {
		const ModelFixture fixture;
		const SlotReader reader{fixture.song, kSynth, fixture.facts};
		expect(reader.read("no/such/path").has_value()).to_be_false();
		expect(reader.reader()("no/such/path").has_value()).to_be_false();
	});

	it("resolves nothing once the slot is gone", _ {
		ModelFixture fixture;
		fixture.song.drum_structure.find(kDrum.id)->instrument.reset();
		const SlotReader reader{fixture.song, kDrum, fixture.facts};
		expect(reader.resolved()).to_be_false();
		expect(reader.read("polyphonic").has_value()).to_be_false();
	});

	it("builds the host contract's context from the slot's kind and the runtime facts", _ {
		ModelFixture fixture;
		fixture.facts.inputs = true;
		fixture.facts.loaded[kDrum.id] = 0b10;
		const SettingContext synth = setting_context(fixture.song, kSynth, fixture.facts);
		expect(synth.owner == SettingContext::Owner::TRACK).to_be_true();
		expect(synth.instrument == SettingContext::Instrument::SYNTH).to_be_true();
		expect(synth.audio_inputs_present).to_be_true();
		const SettingContext lane = setting_context(fixture.song, kDrum, fixture.facts);
		expect(lane.owner == SettingContext::Owner::KIT_LANE).to_be_true();
		expect(lane.instrument == SettingContext::Instrument::SOUND_DRUM).to_be_true();
		expect(static_cast<int32_t>(lane.loaded_sources)).to_equal(0b10);
		expect(deluge::engine::context_is_coherent(synth) && deluge::engine::context_is_coherent(lane)).to_be_true();
		expect(fixture.facts.asked.size()).to_equal(2u);
	});
});

CPPSPEC_SPEC(bind_reader)
