/// @file
/// @brief `SetSetting` over a booted song: each setting group's Edit writes its record, then projects it onto the
///        engine object, and the `*SettingFor` helpers reach an owner outside the song through its setter alone.

#include "deluge-project/setting_group.h"
#include "deluge-project/song_codec.h"
#include "dsp/reverb/reverb.hpp"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/audio_clip.h"
#include "model/edits/set_setting.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
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
	it("renames a kit row through its lane record", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		const auto lane = deluge::bridge::laneAddressOf(*drum, *f.song);
		expect(lane.has_value()).to_be_true();
		deluge::edits::laneSettingFor(*drum, "name", Value::makeScalar("KICK"));
		expect(recorded(*f.song, {SettingGroupId::LANE, *lane}, "name")).to_equal(std::string{"KICK"});
		expect(projected(*f.song, {SettingGroupId::LANE, *lane}, "name")).to_equal(std::string{"KICK"});
	});

	it("sets a note row's direction in its record and on the row", _ {
		spec::SongFixture f = spec::make_synth_song();
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = f.song->setupModelStackWithCurrentClip(memory);
		NoteRow* row = instrumentClipView.createNoteRowForYDisplay(stack, 0)->getNoteRowAllowNull();
		expect(row != nullptr).to_be_true();
		const auto address = deluge::bridge::noteRowAddressOf(*row, *f.clip, *f.song);
		expect(address.has_value()).to_be_true();
		deluge::edits::noteRowSettingFor(*row, *f.clip, "sequenceDirection", Value::makeScalar("reverse"));
		expect(recorded(*f.song, {SettingGroupId::NOTE_ROW, *address}, "sequenceDirection")).to_equal(std::string{"reverse"});
		expect(projected(*f.song, {SettingGroupId::NOTE_ROW, *address}, "sequenceDirection")).to_equal(std::string{"reverse"});
	});
	it("writes every audio clip path through SetSetting and keeps record and clip equal", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const auto clip = deluge::bridge::audioClipAddressOf(*f.clip, *f.song);
		expect(clip.has_value()).to_be_true();
		const SettingAddress address{SettingGroupId::AUDIO_CLIP, *clip};
		for (const auto& row : deluge::project::table_of(SettingGroupId::AUDIO_CLIP)) {
			if (row.path.ends_with("SamplePos")) {
				continue; // a range needs a sample: marker_scene_spec moves the markers
			}
			// Away from the default, so a write the engine never sees shows.
			const int32_t v = row.kind == deluge::engine::SettingKind::BOOL ? 1 - row.default_value
			                  : row.default_value == row.max                  ? row.min
			                                                                  : row.max;
			deluge::edits::audioClipSettingFor(*f.clip, row.path, Value::makeNumber(v));
			expect(std::string{row.path} + "=" + recorded(*f.song, address, row.path))
			    .to_equal(std::string{row.path} + "=" + std::to_string(v));
			expect(std::string{row.path} + "=" + projected(*f.song, address, row.path))
			    .to_equal(std::string{row.path} + "=" + std::to_string(v));
		}
	});
});
// clang-format on

DELUGE_SPEC(song_settings_edit_spec, song_settings_edit)
