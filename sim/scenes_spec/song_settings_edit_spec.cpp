/// @file
/// @brief `SetSetting` over a booted song: each setting group's Edit writes its record, then projects it onto the
///        engine object, and the `*SettingFor` helpers reach an owner outside the song through its setter alone.

#include "deluge-project/setting_group.h"
#include "deluge-project/song_codec.h"
#include "dsp/reverb/reverb.hpp"
#include "extern.h"
#include "gui/menu_item/arpeggiator/mode.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/system.h"
#include "model/clip/audio_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/arp_setting_for.h"
#include "model/edits/bend_range_for.h"
#include "model/edits/set_setting.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/setting_projection.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/roots.h"
#include "scenes/menu/nodes/sound_arp.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <string>

#include "spec/support/spec_main.h"

namespace spec = deluge::scenes::spec;

extern deluge::gui::menu_item::arpeggiator::Mode arpModeMenu;
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

/// @brief Call a legacy item's own `writeCurrentValue()`, which is protected: the pointer-to-member is formed inside a
///        class derived from `Value<T>`.
template <typename T>
struct ValuePeek : deluge::gui::menu_item::Value<T> {
	static void write(deluge::gui::menu_item::Value<T>& value) { (value.*(&ValuePeek::writeCurrentValue))(); }
};

/// @brief Make @p f's clip the one its instrument plays.
void activate(spec::SongFixture& f) {
	char memory[MODEL_STACK_MAX_SIZE];
	f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip));
}

/// @brief Hold a note in @p arp, as a key held into a playing arp does.
void holdNote(ArpeggiatorBase& arp, ArpeggiatorSettings& settings) {
	ArpReturnInstruction instruction;
	arp.noteOn(&settings, 60, 100, &instruction, MIDI_CHANNEL_NONE, zeroMPEValues);
}

/// @brief Whether MIDI is queued to go out, emptying the queue.
bool midiQueued() {
	uint32_t due = 0;
	CriticalSectionGuard guard;
	const bool queued = midiEngine.nextMIDIDue(due, guard);
	midiEngine.flushAllQueuedMIDI(guard);
	return queued;
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
	it("turns a row obeying its clip to reverse from the mirror of the clip's position, as the row's setter does", _ {
		spec::SongFixture f = spec::make_synth_song();
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = f.song->setupModelStackWithCurrentClip(memory);
		NoteRow* row = instrumentClipView.createNoteRowForYDisplay(stack, 0)->getNoteRowAllowNull();
		expect(deluge::bridge::noteRowAddressOf(*row, *f.clip, *f.song).has_value()).to_be_true();
		f.clip->lastProcessedPos = 10;
		row->lastProcessedPosIfIndependent = 999; // stale: the row has been following its clip
		deluge::edits::noteRowSettingFor(*row, *f.clip, "sequenceDirection", Value::makeScalar("reverse"));
		expect(row->lastProcessedPosIfIndependent).to_equal(f.clip->loopLength() - 10);
	});
	it("refuses, outside the song too, a value the setting's row refuses", _ {
		spec::SongFixture f = spec::make_midi_song();
		MIDIInstrument loose; // not in the song: the setter alone would take it
		loose.setChannel(2);
		deluge::edits::trackSettingFor(loose, "channel", Value::makeNumber(99));
		expect(loose.getChannel()).to_equal(2);
		deluge::edits::trackSettingFor(loose, "channelSuffix", Value::makeScalar("x"));
		expect(loose.channelSuffix).to_equal(-1);
	});
	it("leaves the record as it was when the engine refuses the value", _ {
		spec::SongFixture f = spec::make_synth_song();
		const auto track = deluge::bridge::trackAddressOf(*f.instrument, *f.song);
		expect(track.has_value()).to_be_true();
		const SettingAddress device{SettingGroupId::INSTRUMENT_DEVICE, *track};
		const std::string before = recorded(*f.song, device, "collapseMpe");
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetSetting edit{device, "collapseMpe", Value::makeNumber(before == "1" ? 0 : 1)};
		expect(static_cast<int>(deluge::edits::apply(ctx, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(recorded(*f.song, device, "collapseMpe")).to_equal(before);
	});

	it("changes nothing, and does not fault, with no current song", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const song = currentSong;
		currentSong = nullptr;
		deluge::edits::songSettingFor("swingInterval", Value::makeNumber(5));
		deluge::edits::trackSettingFor(*f.instrument, "name", Value::makeScalar("NOSONG"));
		currentSong = song;
		expect(f.instrument->name == "NOSONG").to_be_false();
	});
	it("renames a clip in its record and on the clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		const auto clip = deluge::bridge::clipAddressOf(*f.clip, *f.song);
		expect(clip.has_value()).to_be_true();
		deluge::edits::clipSettingFor(*f.clip, "name", Value::makeScalar("VERSE"));
		expect(recorded(*f.song, {SettingGroupId::CLIP, *clip}, "name")).to_equal(std::string{"VERSE"});
		expect(f.clip->name).to_equal(std::string{"VERSE"});
	});

	it("turns a clip to reverse in its record, from the mirror of its position", _ {
		spec::SongFixture f = spec::make_synth_song();
		const auto clip = deluge::bridge::clipAddressOf(*f.clip, *f.song);
		f.clip->lastProcessedPos = 10;
		deluge::edits::clipSettingFor(*f.clip, "sequenceDirection", Value::makeScalar("reverse"));
		expect(recorded(*f.song, {SettingGroupId::CLIP, *clip}, "sequenceDirection")).to_equal(std::string{"reverse"});
		expect(f.clip->lastProcessedPos).to_equal(f.clip->loopLength() - 10);
		expect(f.clip->sequenceDirectionMode == SequenceDirection::REVERSE).to_be_true();
	});
	it("writes a clip value into the clip's layer and onto the clip", _ {
		spec::SongFixture f = spec::make_midi_song();
		const auto clip = deluge::bridge::clipAddressOf(*f.clip, *f.song);
		expect(clip.has_value()).to_be_true();
		const SettingAddress values{SettingGroupId::CLIP_VALUES, *clip};
		expect(recorded(*f.song, values, "midiPGM")).to_equal(std::string{"128"}); // none, until the clip holds one
		deluge::edits::clipValueSettingFor(*f.clip, "midiPGM", Value::makeNumber(5));
		expect(recorded(*f.song, values, "midiPGM")).to_equal(std::string{"5"});
		expect(projected(*f.song, values, "midiPGM")).to_equal(std::string{"5"});
	});
	it("sends a program change for the active MIDI clip as its program changes", _ {
		spec::SongFixture f = spec::make_midi_song();
		char memory[MODEL_STACK_MAX_SIZE];
		f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip));
		expect(f.clip->isActiveOnOutput()).to_be_true();
		uint32_t due = 0;
		{
			CriticalSectionGuard guard;
			midiEngine.flushAllQueuedMIDI(guard);
			expect(midiEngine.nextMIDIDue(due, guard)).to_be_false();
		}
		deluge::edits::clipValueSettingFor(*f.clip, "midiPGM", Value::makeNumber(5));
		CriticalSectionGuard guard;
		expect(midiEngine.nextMIDIDue(due, guard)).to_be_true();
		midiEngine.flushAllQueuedMIDI(guard);
	});

	it("gives a clone the clip's program", _ {
		spec::SongFixture f = spec::make_midi_song();
		deluge::edits::clipValueSettingFor(*f.clip, "midiPGM", Value::makeNumber(9));
		InstrumentClip copy(*f.song);
		copy.copyBasicsFrom(f.clip);
		expect(copy.docClip_->values.midi_pgm.value_or(-1)).to_equal(9);
		expect(static_cast<int>(copy.midiPGM)).to_equal(9);
	});
	it("changes an active MIDI clip's bend range so its outgoing bends use it at once", _ {
		spec::SongFixture f = spec::make_midi_song();
		char memory[MODEL_STACK_MAX_SIZE];
		f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip));
		const SettingAddress values{SettingGroupId::CLIP_VALUES, *deluge::bridge::clipAddressOf(*f.clip, *f.song)};
		deluge::edits::clipValueSettingFor(*f.clip, "bendRange", Value::makeNumber(12));
		expect(recorded(*f.song, values, "bendRange")).to_equal(std::string{"12"});
		expect(projected(*f.song, values, "bendRange")).to_equal(std::string{"12"});
		expect(static_cast<int>(static_cast<MIDIInstrument*>(f.instrument)->cachedBendRanges[BEND_RANGE_MAIN])).to_equal(12);
	});

	it("keeps a kit row's finger bend range on its note row", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		NoteRow* row = f.clip->getNoteRowForDrum(drum);
		expect(row != nullptr).to_be_true();
		deluge::edits::bendRangeFor(*f.clip, row->paramManager, BEND_RANGE_FINGER_LEVEL, 24);
		const auto address = deluge::bridge::noteRowAddressOf(*row, *f.clip, *f.song);
		expect(recorded(*f.song, {SettingGroupId::NOTE_ROW, *address}, "bendRange")).to_equal(std::string{"24"});
		expect(projected(*f.song, {SettingGroupId::NOTE_ROW, *address}, "bendRange")).to_equal(std::string{"24"});
	});
	it("sets the sync-scaling clip by its id and re-derives the tick magnitude", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::songSettingFor("syncScalingClip", Value::makeNumber(f.clip->id()));
		expect(recorded(*f.song, kSong, "syncScalingClip")).to_equal(std::to_string(f.clip->id()));
		expect(f.song->getSyncScalingClip() == f.clip).to_be_true();
		const int32_t magnitude = f.song->project.transport.inside_world_tick_magnitude;
		f.song->setInputTickScaleClip(nullptr);
		f.song->setInputTickScaleClip(f.clip);
		expect(f.song->project.transport.inside_world_tick_magnitude).to_equal(magnitude);
		deluge::edits::songSettingFor("syncScalingClip", Value::makeNumber(-1));
		expect(f.song->getSyncScalingClip() == nullptr).to_be_true();
	});

	it("clears the sync-scaling clip's id when the clip goes, leaving the tempo as it was", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::songSettingFor("syncScalingClip", Value::makeNumber(f.clip->id()));
		const int32_t magnitude = f.song->project.transport.inside_world_tick_magnitude;
		// The song's own delete: out of its list, then retired.
		ClipArray& clips = f.song->live.clips.session_clips;
		clips.erase(clips.begin() + clips.getIndexForClip(f.clip));
		f.song->deleteClipObject(f.clip, false, InstrumentRemoval::NONE);
		expect(recorded(*f.song, kSong, "syncScalingClip")).to_equal(std::string{"-1"});
		expect(f.song->getSyncScalingClip() == nullptr).to_be_true();
		expect(f.song->project.transport.inside_world_tick_magnitude).to_equal(magnitude);
	});
	it("records an audio track's input and the track it records from, by id", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		AudioOutput* source = f.song->createNewAudioOutput();
		const auto track = deluge::bridge::trackAddressOf(*f.output, *f.song);
		expect(track.has_value()).to_be_true();
		const SettingAddress input{SettingGroupId::AUDIO_INPUT, *track};
		deluge::edits::audioInputSettingFor(*f.output, "inputChannel", Value::makeScalar("specificTrack"));
		deluge::edits::audioInputSettingFor(*f.output, "outputRecordingFrom", Value::makeNumber(*source->id()));
		deluge::edits::audioInputSettingFor(*f.output, "mode", Value::makeScalar("Looper/FX"));
		expect(recorded(*f.song, input, "inputChannel")).to_equal(std::string{"specificTrack"});
		expect(recorded(*f.song, input, "outputRecordingFrom")).to_equal(std::to_string(*source->id()));
		expect(recorded(*f.song, input, "mode")).to_equal(std::string{"Looper/FX"});
		expect(f.output->getOutputRecordingFrom() == source).to_be_true();
		expect(f.output->mode == AudioOutputMode::looper).to_be_true();
		expect(projected(*f.song, input, "outputRecordingFrom")).to_equal(std::to_string(*source->id()));
	});

	it("clears the record that pointed at a source track when the source goes", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		AudioOutput* source = f.song->createNewAudioOutput();
		const SettingAddress input{SettingGroupId::AUDIO_INPUT, *deluge::bridge::trackAddressOf(*f.output, *f.song)};
		deluge::edits::audioInputSettingFor(*f.output, "inputChannel", Value::makeScalar("specificTrack"));
		deluge::edits::audioInputSettingFor(*f.output, "outputRecordingFrom", Value::makeNumber(*source->id()));
		f.song->clearRecordingFromReferencesTo(source);
		expect(recorded(*f.song, input, "outputRecordingFrom")).to_equal(std::string{"-1"});
		expect(f.output->getOutputRecordingFrom() == nullptr).to_be_true();
	});
	it("saves hide-unlabeled even on a MIDI track never otherwise edited", _ {
		spec::SongFixture f = spec::make_midi_song();
		auto& midi = *static_cast<MIDIInstrument*>(f.instrument);
		midi.editedByUser = false;
		deluge::edits::trackSettingFor(midi, "hideUnlabeledCC", Value::makeNumber(1));
		const SettingAddress device = track_of(*f.song, midi, "hideUnlabeledCC");
		expect(recorded(*f.song, device, "hideUnlabeledCC")).to_equal(std::string{"1"});
		expect(midi.hideUnlabeledCC).to_be_true();
		expect(midi.editedByUser).to_be_true(); // the device element is only saved for an edited track
	});

	it("keeps a CC label in the record and on the instrument", _ {
		spec::SongFixture f = spec::make_midi_song();
		auto& midi = *static_cast<MIDIInstrument*>(f.instrument);
		deluge::edits::trackSettingFor(midi, "ccLabels/20", Value::makeScalar("FILTER"));
		const SettingAddress device = track_of(*f.song, midi, "ccLabels/20");
		expect(recorded(*f.song, device, "ccLabels/20")).to_equal(std::string{"FILTER"});
		expect(std::string{midi.getNameFromCC(20)}).to_equal(std::string{"FILTER"});
	});
	it("leaves a clip value it refuses unheld, so the clip keeps playing its track's", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const auto clip = deluge::bridge::clipAddressOf(*f.clip, *f.song);
		expect(clip.has_value()).to_be_true();
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetSetting edit{{SettingGroupId::CLIP_VALUES, *clip}, "midiPGM", Value::makeNumber(5)};
		expect(static_cast<int>(deluge::edits::apply(ctx, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(f.clip->docClip_->values.midi_pgm.has_value()).to_be_false();
	});

	it("sets a clip's arp in its record and on the clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/numOctaves", Value::makeNumber(3));
		const SettingAddress arp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*f.clip, *f.song)};
		expect(recorded(*f.song, arp, "arpeggiator/numOctaves")).to_equal(std::string{"3"});
		expect(static_cast<int>(f.clip->arpSettings.numOctaves)).to_equal(3);
		expect(projected(*f.song, arp, "arpeggiator/numOctaves")).to_equal(std::string{"3"});
	});

	it("chooses a preset as its three fields, and flags the arp to restart", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.clip->arpSettings.flagForceArpRestart = false;
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/preset", Value::makeScalar("down"));
		expect(f.clip->arpSettings.mode == ArpMode::ARP).to_be_true();
		expect(f.clip->arpSettings.octaveMode == ArpOctaveMode::DOWN).to_be_true();
		expect(f.clip->arpSettings.noteMode == ArpNoteMode::DOWN).to_be_true();
		expect(f.clip->arpSettings.preset == ArpPreset::DOWN).to_be_true();
		expect(f.clip->arpSettings.flagForceArpRestart).to_be_true();
		const SettingAddress arp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*f.clip, *f.song)};
		expect(recorded(*f.song, arp, "arpeggiator/noteMode")).to_equal(std::string{"down"});
	});

	it("shows the custom preset a clip's arp was given, so its menu chains to the mode pages", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/preset", Value::makeScalar("custom"));
		expect(f.clip->arpSettings.preset == ArpPreset::CUSTOM).to_be_true();
		expect(f.clip->arpSettings.mode == ArpMode::ARP).to_be_true();
	});

	it("re-derives the preset a clip's arp shows when its note mode changes", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/preset", Value::makeScalar("up"));
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/noteMode", Value::makeScalar("upDown"));
		expect(f.clip->arpSettings.preset == ArpPreset::CUSTOM).to_be_true();
	});

	it("sets a kit row's arp in its lane record", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		deluge::edits::laneArpSettingFor(*drum, "arpeggiator/arpMode", Value::makeScalar("arp"));
		const SettingAddress arp{SettingGroupId::LANE_ARP, *deluge::bridge::laneAddressOf(*drum, *f.song)};
		expect(recorded(*f.song, arp, "arpeggiator/arpMode")).to_equal(std::string{"arp"});
		expect(drum->arpSettings.mode == ArpMode::ARP).to_be_true();
	});

	it("holds a MIDI clip's arp rate, and refuses one on a synth clip, where the rate is a param", _ {
		spec::SongFixture midi = spec::make_midi_song();
		deluge::edits::clipArpSettingFor(*midi.clip, "arpeggiator/rate", Value::makeNumber(12345));
		const SettingAddress midiArp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*midi.clip, *midi.song)};
		expect(recorded(*midi.song, midiArp, "arpeggiator/rate")).to_equal(std::string{"12345"});
		expect(midi.clip->arpSettings.rate).to_equal(12345);

		spec::SongFixture synth = spec::make_synth_song();
		const int32_t before = synth.clip->arpSettings.rate;
		deluge::edits::clipArpSettingFor(*synth.clip, "arpeggiator/rate", Value::makeNumber(12345));
		const SettingAddress synthArp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*synth.clip, *synth.song)};
		expect(deluge::project::holds_setting(synth.song->project, synthArp, "arpeggiator/rate")).to_be_false();
		expect(synth.clip->arpSettings.rate).to_equal(before);
	});

	it("turning a playing MIDI clip's arp on or off sends its notes off", _ {
		spec::SongFixture f = spec::make_midi_song();
		activate(f);
		(void)midiQueued();
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/arpMode", Value::makeScalar("arp"));
		expect(midiQueued()).to_be_true();
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/numOctaves", Value::makeNumber(4));
		expect(midiQueued()).to_be_false(); // only turning the arp on or off silences it
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/preset", Value::makeScalar("off"));
		expect(midiQueued()).to_be_true();
	});

	it("turning a playing synth's arp off lets go of the notes it held", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(f);
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/arpMode", Value::makeScalar("arp"));
		ArpeggiatorBase& arp = *static_cast<SoundInstrument*>(f.instrument)->getArp();
		holdNote(arp, f.clip->arpSettings);
		expect(arp.hasAnyInputNotesActive()).to_be_true();
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/arpMode", Value::makeScalar("off"));
		expect(arp.hasAnyInputNotesActive()).to_be_false();
	});

	it("turning a playing kit row's arp off lets go of the notes it held", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		activate(f);
		deluge::edits::laneArpSettingFor(*drum, "arpeggiator/arpMode", Value::makeScalar("arp"));
		holdNote(drum->arpeggiator, drum->arpSettings);
		expect(static_cast<ArpeggiatorBase&>(drum->arpeggiator).hasAnyInputNotesActive()).to_be_true();
		deluge::edits::laneArpSettingFor(*drum, "arpeggiator/arpMode", Value::makeScalar("off"));
		expect(static_cast<ArpeggiatorBase&>(drum->arpeggiator).hasAnyInputNotesActive()).to_be_false();
	});

	it("turning a playing kit's whole-kit arp off silences every row", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		activate(f);
		deluge::edits::laneArpSettingFor(*drum, "arpeggiator/arpMode", Value::makeScalar("arp"));
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/arpMode", Value::makeScalar("arp"));
		holdNote(drum->arpeggiator, drum->arpSettings);
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/arpMode", Value::makeScalar("off"));
		expect(static_cast<ArpeggiatorBase&>(drum->arpeggiator).hasAnyInputNotesActive()).to_be_false();
	});

	it("sets every kit row's arp, sound and MIDI alike, from the legacy mode item with affect-entire held", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* sound = spec::add_drum<SoundDrum>(f);
		MIDIDrum* midi = spec::add_drum<MIDIDrum>(f);
		f.song->setCurrentClip(f.clip);
		soundEditor.currentArpSettings = &midi->arpSettings;
		soundEditor.setupKitGlobalFXMenu = false;
		currentUIMode = UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR;
		arpModeMenu.setValue(1);
		ValuePeek<int32_t>::write(arpModeMenu);
		currentUIMode = UI_MODE_NONE;
		for (Drum* drum : {static_cast<Drum*>(sound), static_cast<Drum*>(midi)}) {
			const SettingAddress arp{SettingGroupId::LANE_ARP, *deluge::bridge::laneAddressOf(*drum, *f.song)};
			expect(recorded(*f.song, arp, "arpeggiator/arpMode")).to_equal(std::string{"arp"});
			expect(drum->arpSettings.mode == ArpMode::ARP).to_be_true();
		}
	});

	it("turning a playing MIDI clip's arp off from the scenes sends its notes off, and records it", _ {
		spec::SongFixture f = spec::make_midi_song();
		activate(f);
		f.song->setCurrentClip(f.clip);
		const auto ctx = deluge::scenes::menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		deluge::scenes::menu::nodes::set_arp_mode(ctx, 1);
		(void)midiQueued();
		deluge::scenes::menu::nodes::set_arp_mode(ctx, 0);
		expect(midiQueued()).to_be_true();
		const SettingAddress arp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*f.clip, *f.song)};
		expect(recorded(*f.song, arp, "arpeggiator/arpMode")).to_equal(std::string{"off"});
	});

	it("records a MIDI clip's arp rate set from the scenes", _ {
		spec::SongFixture f = spec::make_midi_song();
		f.song->setCurrentClip(f.clip);
		const auto ctx = deluge::scenes::menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		deluge::scenes::menu::nodes::set_arp_rate_midi(ctx, 40);
		const SettingAddress arp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*f.clip, *f.song)};
		expect(recorded(*f.song, arp, "arpeggiator/rate")).to_equal(std::to_string(f.clip->arpSettings.rate));
		expect(deluge::project::holds_setting(f.song->project, arp, "arpeggiator/rate")).to_be_true();
	});

	it("gives a cloned clip its arp, in its record and on the clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/numOctaves", Value::makeNumber(6));
		InstrumentClip copy(*f.song);
		copy.copyBasicsFrom(f.clip);
		expect(copy.docClip_->values.arp.has_value()).to_be_true();
		expect(static_cast<int>(copy.docClip_->values.arp->num_octaves)).to_equal(6);
		expect(static_cast<int>(copy.arpSettings.numOctaves)).to_equal(6);
	});

	it("records the default arp of the instrument a clip is given", _ {
		Song* song = spec::detail::make_bare_song();
		InstrumentClip* clip = spec::detail::make_session_clip(song);
		ParamManagerForTimeline params;
		auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		synth->setupAsDefaultSynth(&params);
		synth->defaultArpSettings.mode = ArpMode::ARP;
		synth->defaultArpSettings.numOctaves = 5;
		spec::detail::attach(song, clip, synth, params);
		const SettingAddress arp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*clip, *song)};
		expect(recorded(*song, arp, "arpeggiator/arpMode")).to_equal(std::string{"arp"});
		expect(recorded(*song, arp, "arpeggiator/numOctaves")).to_equal(std::string{"5"});
	});

	it("keeps an arp's other settings when one changes on a clip whose arp was never recorded", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.clip->docClip_->values.arp.reset();
		f.clip->arpSettings.syncLevel = SyncLevel::SIXTY_FOURTH;
		deluge::edits::clipArpSettingFor(*f.clip, "arpeggiator/numOctaves", Value::makeNumber(3));
		const SettingAddress arp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*f.clip, *f.song)};
		expect(recorded(*f.song, arp, "arpeggiator/syncLevel")).to_equal(std::string{"7"});
		expect(f.clip->arpSettings.syncLevel == SyncLevel::SIXTY_FOURTH).to_be_true();
	});

	it("records a new kit row's arp as the row holds it", _ {
		spec::SongFixture f = spec::make_kit_song();
		GateDrum* gate = spec::add_drum<GateDrum>(f);
		const SettingAddress arp{SettingGroupId::LANE_ARP, *deluge::bridge::laneAddressOf(*gate, *f.song)};
		expect(recorded(*f.song, arp, "arpeggiator/numOctaves")).to_equal(std::string{"1"});
	});

	it("leaves a clip with no arp record without one when its arp refuses a value", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.clip->docClip_->values.arp.reset();
		const SettingAddress arp{SettingGroupId::CLIP_ARP, *deluge::bridge::clipAddressOf(*f.clip, *f.song)};
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetSetting edit{arp, "arpeggiator/rate", Value::makeNumber(5)}; // a param on a synth
		expect(static_cast<int>(deluge::edits::apply(ctx, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(f.clip->docClip_->values.arp.has_value()).to_be_false();
	});

	it("keeps an arp's modes when a refused preset is rolled back", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::project::DocClip& orphan = f.song->project.clip_structure.getOrCreate(4242); // no engine clip
		deluge::project::ArpRecord& record = orphan.values.arp.emplace();
		record.mode = 1;
		record.octave_mode = 2;
		record.note_mode = 4;
		const deluge::project::ArpRecord before = record;
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetSetting edit{{SettingGroupId::CLIP_ARP, deluge::project::ClipAddress{.clip_id = 4242}},
		                               "arpeggiator/preset", Value::makeScalar("down")};
		expect(static_cast<int>(deluge::edits::apply(ctx, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(*orphan.values.arp == before).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(song_settings_edit_spec, song_settings_edit)
