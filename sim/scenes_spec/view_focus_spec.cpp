// Legacy View's gold-knob focus, as addresses: every setter shape names the owner its stack names (a synth, a kit row,
// a kit's chain, an audio track, the song, a MIDI track, nothing for a CV track or a release, an arranger row's
// backup with no clip), and a setter leaves the focus's own view state (the region, the compressor view, the VU meter)
// as it was.

#include "gui/views/knob_focus_from_stack.h"
#include "gui/views/view.h"

#include "deluge-project/knob_focus.h"
#include "engines/deluge/params.h"
#include "io/midi/midi_device_manager.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/learn_mapping.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>

namespace {

namespace project = deluge::project;
namespace spec = deluge::scenes::spec;
namespace params = deluge::modulation::params;

/// @brief View's focus owner now.
const project::KnobFocus& focus() {
	return view.knobFocus();
}

/// @brief A second synth on a second session clip of @p f's song.
spec::SongFixture add_synth_clip(const spec::SongFixture& f) {
	spec::SongFixture other{.song = f.song, .clip = spec::detail::make_session_clip(f.song)};
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->setupAsDefaultSynth(&params);
	spec::detail::attach(f.song, other.clip, synth, params);
	other.instrument = synth;
	return other;
}

/// @brief Learn CC 74 on channel 1 of DIN to @p f's synth's volume, and send it @p value on @p f's clip.
void learnAndSendVolume(const spec::SongFixture& f, uint8_t value) {
	auto& sound = *static_cast<SoundInstrument*>(f.instrument);
	ParamDescriptor volume;
	volume.setToHaveParamOnly(params::LOCAL_VOLUME);
	if (sound.midi_knobs.empty()) {
		(void)deluge::edits::learnKnobFor(sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1);
	}
	char memory[MODEL_STACK_MAX_SIZE];
	sound.offerReceivedCCToLearnedParamsForClip({project::ControlsAddress::Kind::OUTPUT, *f.instrument->id()},
	                                            MIDIDeviceManager::dinMIDIPorts, 1, 74, value,
	                                            setupModelStackWithTimelineCounter(memory, f.song, f.clip), -1);
}

/// @brief Is @p f's clip's synth volume automated?
bool volumeAutomated(const spec::SongFixture& f) {
	const deluge::project::DocAutoParam* param =
	    f.song->project.resolveParam({.owner = project::OwnerId::instrument(*f.instrument->id()),
	                                  .space = project::ParamSpace::ENGINE,
	                                  .id = deluge::instrument::param::kVolume,
	                                  .clip_id = f.clip->id()});
	return param != nullptr && !param->nodes.empty();
}

} // namespace

// clang-format off
describe view_focus("legacy View's gold-knob focus, as addresses", ${
	it("names a synth on its clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const project::KnobFocus want{.owner = project::SoundFocus{
		    .slot = {project::SlotAddress::Kind::OUTPUT, *f.instrument->id()}, .clip = project::ClipAddress{f.clip->id()}}};
		expect(focus().owner == want.owner).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("names a kit row with its note row and kit, or the kit's chain with affect-entire", _ {
		spec::SongFixture f = spec::make_kit_song();
		currentSong = f.song;
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		auto* kit = static_cast<Kit*>(f.instrument);
		kit->selectedDrum = drum;
		f.clip->affectEntire = false;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const project::KnobFocus row{.owner = project::SoundFocus{
		    .slot = {project::SlotAddress::Kind::DRUM, *drum->id()},
		    .clip = project::ClipAddress{f.clip->id()},
		    .row = project::NoteRowAddress{f.clip->id(), true, static_cast<int32_t>(*drum->id())},
		    .kit = *kit->id()}};
		expect(focus().owner == row.owner).to_be_true();

		f.clip->affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const project::KnobFocus chain{.owner = project::ChainFocus{
		    .chain = {project::ChainAddress::Kind::OUTPUT, *kit->id()},
		    .owner = project::ChainOwner::KIT,
		    .clip = project::ClipAddress{f.clip->id()}}};
		expect(focus().owner == chain.owner).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("names an audio track's chain on its clip", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		currentSong = f.song;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const project::KnobFocus want{.owner = project::ChainFocus{
		    .chain = {project::ChainAddress::Kind::OUTPUT, *f.output->id()},
		    .owner = project::ChainOwner::AUDIO_TRACK,
		    .clip = project::ClipAddress{f.clip->id()}}};
		expect(focus().owner == want.owner).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("names the song, a MIDI track on its clip, and nothing for a CV track or a release", _ {
		spec::SongFixture song = spec::make_synth_song();
		currentSong = song.song;
		song.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(song.song, false);
		expect(std::holds_alternative<project::SongFocus>(focus().owner)).to_be_true();
		view.releaseActiveModControllable();
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();

		spec::SongFixture midi = spec::make_midi_song();
		currentSong = midi.song;
		view.setActiveModControllableTimelineCounter(midi.clip, false);
		const project::KnobFocus want{.owner = project::MidiFocus{
		    .track = project::TrackAddress{*midi.instrument->id()}, .clip = project::ClipAddress{midi.clip->id()}}};
		expect(focus().owner == want.owner).to_be_true();

		spec::SongFixture cv = spec::make_cv_song();
		currentSong = cv.song;
		view.setActiveModControllableTimelineCounter(cv.clip, false);
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("names an arranger row's backup with no clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		ParamManagerForTimeline params;
		auto* row = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		row->setupAsDefaultSynth(&params);
		f.song->live.param_backup.backUp(*row, nullptr, &params, true);
		f.song->addOutput(row);
		view.setActiveModControllableWithoutTimelineCounter(row, f.song->live.param_backup.getPreferablyWithClip(*row, nullptr));
		const project::KnobFocus want{.owner = project::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, *row->id()}}};
		expect(focus().owner == want.owner).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("keeps its own view state -- the region, the compressor view, the VU meter -- across a setter", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 0);
		view.knobFocus().comp = project::CompView{.editing = true, .param = 2};
		view.knobFocus().vu_meter = true;
		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		expect(focus().region == project::ModRegion{.pos = 96, .length = 48}).to_be_true();
		expect(focus().comp == project::CompView{.editing = true, .param = 2}).to_be_true();
		expect(focus().vu_meter).to_be_true();
		view.knobFocus().comp = {};
		view.knobFocus().vu_meter = false;
		view.setModRegion();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("answers which clip and kit row the knobs are on, by id, in their own song only", _ {
		spec::SongFixture f = spec::make_kit_song();
		currentSong = f.song;
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		SoundDrum* other = spec::add_drum<SoundDrum>(f);
		auto* kit = static_cast<Kit*>(f.instrument);
		kit->selectedDrum = drum;
		f.clip->affectEntire = false;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		expect(view.knobsOnClip(*f.song, *f.clip)).to_be_true();
		expect(view.knobsOnDrum(*f.song, *drum)).to_be_true();
		expect(view.knobsOnDrum(*f.song, *other)).to_be_false();
		expect(view.knobsOnClipContext()).to_be_true();

		spec::SongFixture elsewhere = spec::make_kit_song(); // its clip's id may equal f's
		currentSong = f.song;
		expect(view.knobsOnClip(*elsewhere.song, *elsewhere.clip)).to_be_false();

		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		expect(view.knobsOnClip(*f.song, *f.clip)).to_be_false();
		expect(view.knobsOnDrum(*f.song, *drum)).to_be_false();
		expect(view.knobsOnClipContext()).to_be_false();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("keeps a CV track in the clip context though its knobs move nothing", _ {
		spec::SongFixture cv = spec::make_cv_song();
		currentSong = cv.song;
		view.setActiveModControllableTimelineCounter(cv.clip, false);
		expect(view.knobsOnClipContext()).to_be_true();
		view.releaseActiveModControllable();
		expect(view.knobsOnClipContext()).to_be_false();
		currentSong = nullptr;
	});

	it("holds a step-edit region on the clip and row the pad names, and nowhere else", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture b = add_synth_clip(f);
		currentSong = f.song;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 3);
		expect(view.modPos()).to_equal(96u);
		expect(view.modLength()).to_equal(48u);
		expect(view.modNoteRowId()).to_equal(3);
		expect(view.stepEditRegionHolds(*f.clip)).to_be_true();
		expect(view.stepEditRegionHolds(*f.clip, 3)).to_be_true();
		expect(view.stepEditRegionHolds(*f.clip, 4)).to_be_false();
		expect(view.stepEditRegionHolds(*b.clip)).to_be_false();
		view.setModRegion();
		expect(view.stepEditRegionHolds(*f.clip)).to_be_false();
		expect(view.modPos()).to_equal(0xFFFFFFFFu);
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("writes a learned knob's region only on the clip whose note is held", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture b = add_synth_clip(f);
		currentSong = f.song;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 0);
		learnAndSendVolume(b, 100);
		expect(volumeAutomated(b)).to_be_false();
		learnAndSendVolume(f, 100);
		expect(volumeAutomated(f)).to_be_true();
		view.setModRegion();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("lets go of the knobs when their clip or song retires, and only then", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture b = add_synth_clip(f);
		currentSong = f.song;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.releaseKnobsIfOn(*b.clip);
		expect(std::holds_alternative<project::SoundFocus>(focus().owner)).to_be_true();
		view.releaseKnobsIfOn(*f.clip);
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();
		expect(view.activeModControllableModelStack.getTimelineCounterAllowNull() == nullptr).to_be_true();

		view.setActiveModControllableTimelineCounter(f.clip, false);
		spec::SongFixture elsewhere = spec::make_synth_song();
		currentSong = f.song;
		view.releaseKnobsIfOnSong(*elsewhere.song);
		expect(std::holds_alternative<project::SoundFocus>(focus().owner)).to_be_true();
		view.releaseKnobsIfOnSong(*f.song);
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();
		currentSong = nullptr;
	});
});
// clang-format on

CPPSPEC_SPEC(view_focus)
