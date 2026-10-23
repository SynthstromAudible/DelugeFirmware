// Legacy View's gold-knob focus, as addresses: every setter shape names the owner its stack names (a synth, a kit row,
// a kit's chain, an audio track, the song, a MIDI track, nothing for a CV track or a release, an arranger row's
// backup with no clip), and a setter leaves the focus's own view state (the region, the compressor view, the VU meter)
// as it was.

#include "gui/views/knob_focus_from_stack.h"
#include "gui/views/view.h"

#include "deluge-project/knob_focus.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
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

/// @brief View's focus owner now.
const project::KnobFocus& focus() {
	return view.knobFocus();
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
});
// clang-format on

CPPSPEC_SPEC(view_focus)
