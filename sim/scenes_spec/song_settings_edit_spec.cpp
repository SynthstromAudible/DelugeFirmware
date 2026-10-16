/// @file
/// @brief `SetSetting` over a booted song: each setting group's Edit writes its record, then projects it onto the
///        engine object, and the `*SettingFor` helpers reach an owner outside the song through its setter alone.

#include "deluge-project/setting_group.h"
#include "deluge-project/song_codec.h"
#include "dsp/reverb/reverb.hpp"
#include "model/edits/set_setting.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/project_bridge/setting_projection.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <string>

#include "spec/support/spec_main.h"

namespace spec = deluge::scenes::spec;
using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;
using deluge::project::SongAddress;
using deluge::serialization::Value;

namespace {

const SettingAddress kSong{SettingGroupId::SONG, SongAddress{}};

/// @brief The record's value at @p path, as text.
std::string recorded(::Song& song, const SettingAddress& address, std::string_view path) {
	const auto value = deluge::project::read_setting(song.project, address, path);
	return value ? value->str() : std::string{"<none>"};
}

/// @brief @p output's address in its track (@p path "name") or instrument-device group.
SettingAddress track_of(::Song& song, ::Output& output, std::string_view path) {
	return {deluge::bridge::trackGroupOf(path), *deluge::bridge::trackAddressOf(output, song)};
}

/// @brief The engine copy's value at @p path, as text.
std::string projected(::Song& song, const SettingAddress& address, std::string_view path) {
	const auto value = deluge::bridge::engineSettingValue(song, address, path);
	return value ? value->str() : std::string{"<none>"};
}

} // namespace

// clang-format off
describe song_settings_edit("SetSetting", $ {
	it("writes the record and projects it onto the song", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::songSettingFor("swingInterval", Value::makeNumber(5));
		expect(recorded(*f.song, kSong, "swingInterval")).to_equal(std::string{"5"});
		expect(projected(*f.song, kSong, "swingInterval")).to_equal(std::string{"5"});
	});

	it("refuses a value outside the row and leaves the record and the song as they were", _ {
		spec::SongFixture f = spec::make_synth_song();
		const deluge::project::Groove before = f.song->project.groove;
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetSetting edit{kSong, "swingInterval", Value::makeNumber(99)};
		expect(static_cast<int>(deluge::edits::apply(ctx, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(f.song->project.groove == before).to_be_true();
	});
	it("projects every reverb path onto the engine and flags a parameter update", _ {
		spec::SongFixture f = spec::make_synth_song();
		for (const auto& row : deluge::project::table_of(SettingGroupId::SONG)) {
			if (!row.path.starts_with("reverb/")) {
				continue;
			}
			const std::string v = std::to_string(row.max);
			AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
			deluge::edits::songSettingFor(row.path, Value::makeNumber(row.max));
			expect(std::string{row.path} + "=" + recorded(*f.song, kSong, row.path))
			    .to_equal(std::string{row.path} + "=" + v);
			expect(std::string{row.path} + "=" + projected(*f.song, kSong, row.path))
			    .to_equal(std::string{row.path} + "=" + v);
			expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
		}
	});

	it("saves the reverb model from the record", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::songSettingFor("reverb/model", Value::makeNumber(2));
		expect(static_cast<int>(f.song->project.reverb.model))
		    .to_equal(static_cast<int>(deluge::project::ReverbModel::DIGITAL));
		expect(static_cast<int>(AudioEngine::reverb.getModel())).to_equal(static_cast<int>(deluge::dsp::Reverb::Model::DIGITAL));
		const Value saved = deluge::project::encode(f.song->project.reverb, 0);
		expect(saved.find("model") != nullptr && saved.find("model")->str() == "2").to_be_true();
	});

	it("loads a song saved before reverb models as freeverb", _ {
		deluge::project::ReverbSettings settings{.model = deluge::project::ReverbModel::MUTABLE};
		Value file = Value::makeMap();
		file.set("roomSize", Value::makeNumber(0));
		expect(deluge::project::decode(file, settings, {}).has_value()).to_be_true();
		expect(static_cast<int>(settings.model)).to_equal(static_cast<int>(deluge::project::ReverbModel::FREEVERB));
		file.set("model", Value::makeNumber(1));
		expect(deluge::project::decode(file, settings, {}).has_value()).to_be_true();
		expect(static_cast<int>(settings.model)).to_equal(static_cast<int>(deluge::project::ReverbModel::MUTABLE));
	});
	it("disables and re-enables a preset scale in the record", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetSetting disable{kSong, "scales/disabled/3", Value::makeNumber(1)};
		expect(static_cast<int>(deluge::edits::apply(ctx, disable))).to_equal(static_cast<int>(Error::NONE));
		expect(f.song->project.scale.disabled_preset_scales.test(3)).to_be_true();
		deluge::edits::songSettingFor("scales/disabled/3", Value::makeNumber(0));
		expect(f.song->project.scale.disabled_preset_scales.test(3)).to_be_false();
	});
	it("writes every instrument-device path into the record and onto the MIDI instrument", _ {
		spec::SongFixture f = spec::make_midi_song();
		auto& midi = *static_cast<MIDIInstrument*>(f.instrument);
		const std::pair<std::string_view, Value> writes[] = {
		    {"channelSuffix", Value::makeNumber(3)},          {"deviceDefinitionFileName", Value::makeScalar("A.XML")},
		    {"outputMpeY", Value::makeScalar("1")},           {"collapseAftertouch", Value::makeNumber(1)},
		    {"collapseMpe", Value::makeNumber(0)},            {"name", Value::makeScalar("KEYS")},
		};
		for (const auto& [path, value] : writes) {
			deluge::edits::trackSettingFor(midi, path, value);
			const SettingAddress address = track_of(*f.song, midi, path);
			expect(std::string{path} + "=" + recorded(*f.song, address, path)).to_equal(std::string{path} + "=" + value.str());
			expect(std::string{path} + "=" + projected(*f.song, address, path)).to_equal(std::string{path} + "=" + value.str());
		}
		expect(static_cast<int>(midi.outputMPEY)).to_equal(static_cast<int>(CC_EXTERNAL_MOD_WHEEL));
	});

	it("re-channels a MIDI instrument in place", _ {
		spec::SongFixture f = spec::make_midi_song();
		auto& midi = *static_cast<MIDIInstrument*>(f.instrument);
		deluge::edits::trackSettingFor(midi, "channel", Value::makeNumber(4));
		expect(recorded(*f.song, track_of(*f.song, midi, "channel"), "channel")).to_equal(std::string{"4"});
		expect(midi.getChannel()).to_equal(4);
	});

	it("writes a CV instrument's second source", _ {
		spec::SongFixture f = spec::make_cv_song();
		auto& cv = *static_cast<CVInstrument*>(f.instrument);
		deluge::edits::trackSettingFor(cv, "cv2Source", Value::makeNumber(2));
		expect(recorded(*f.song, track_of(*f.song, cv, "cv2Source"), "cv2Source")).to_equal(std::string{"2"});
		expect(static_cast<int>(cv.getCV2Mode())).to_equal(2);
	});

	it("renames a track outside the song with the setter alone", _ {
		spec::SongFixture f = spec::make_midi_song();
		MIDIInstrument loose; // not in the song: no record to write
		deluge::edits::trackSettingFor(loose, "name", Value::makeScalar("LOOSE"));
		expect(loose.name).to_equal(std::string{"LOOSE"});
	});
});
// clang-format on

DELUGE_SPEC(song_settings_edit_spec, song_settings_edit)
