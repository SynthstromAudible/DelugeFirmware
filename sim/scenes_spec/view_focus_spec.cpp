// Legacy View's gold-knob focus, as addresses: every setter shape names the owner its stack names (a synth, a kit row,
// a kit's chain, an audio track, the song, a MIDI track, nothing for a CV track or a release, an arranger row's
// backup with no clip), and a setter leaves the focus's own view state (the region, the compressor view, the VU meter)
// as it was.

#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/knob_focus_from_stack.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"

#include "deluge-project/knob_focus.h"
#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "io/midi/midi_device_manager.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/edits/set_param_value.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/reverb_presets.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "scenes/bind/knob_assignment.h"
#include "storage/storage_manager.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>
#include <string>

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

/// @brief The display View's popups draw on: what it last popped up, kept.
struct StubDisplay final : deluge::hid::Display {
	std::string popup;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const* text, PopupType) override { popup = text; }
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
	void displayNotification(std::string_view, std::optional<std::string_view>) override {}
};

/// @brief The arrangement recording @p f's session clip at 0, for as long as it lives.
struct ArrangementRecording {
	RecordingMode mode = playbackHandler.recording;
	decltype(playbackHandler.playbackState) state = playbackHandler.playbackState;
	explicit ArrangementRecording(const spec::SongFixture& f) {
		f.clip->activeIfNoSolo = true;
		const int32_t at = f.instrument->clipInstances.insertSorted(0).value_or(-1);
		if (at >= 0) {
			f.instrument->clipInstances[at].clip = f.clip;
			f.instrument->clipInstances[at].length = f.clip->loopLength();
		}
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
	}
	ArrangementRecording(const ArrangementRecording&) = delete;
	ArrangementRecording& operator=(const ArrangementRecording&) = delete;
	~ArrangementRecording() {
		playbackHandler.recording = mode;
		playbackHandler.playbackState = state;
	}
};

/// @brief @p f's synth's pan on @p clip (none: its backed-up params), the param mode 0's lower knob turns.
std::optional<int32_t> panOn(const spec::SongFixture& f, std::optional<uint32_t> clip) {
	return deluge::bridge::paramValueAt(*f.song,
	                                    {.owner = project::OwnerId::instrument(*f.instrument->id()),
	                                     .space = project::ParamSpace::ENGINE,
	                                     .id = deluge::instrument::param::kPan,
	                                     .clip_id = clip},
	                                    0xFFFFFFFF);
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
		expect(View::KnobStack{view}.stack == nullptr).to_be_true();

		view.setActiveModControllableTimelineCounter(f.clip, false);
		spec::SongFixture elsewhere = spec::make_synth_song();
		currentSong = f.song;
		view.releaseKnobsIfOnSong(*elsewhere.song);
		expect(std::holds_alternative<project::SoundFocus>(focus().owner)).to_be_true();
		view.releaseKnobsIfOnSong(*f.song);
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();
		currentSong = nullptr;
	});

	it("clones once when View turns a knob while the arrangement records, and leaves the session clip", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		*f.instrument->toModControllable()->getModKnobMode() = 0;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const std::optional<int32_t> session_before = panOn(f, f.clip->id());
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		Clip* recorded = nullptr;
		{
			ArrangementRecording recording{f};
			view.modEncoderAction(0, 5);
			view.modEncoderAction(0, 5);
			recorded = f.instrument->getActiveClip();
		}
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		expect(recorded != nullptr && recorded != f.clip && recorded->beingRecordedFromClip == f.clip).to_be_true();
		expect(panOn(f, f.clip->id()) == session_before).to_be_true();
		if (recorded != nullptr && recorded != f.clip) {
			expect(panOn(f, recorded->id()) != session_before).to_be_true();
		}
		expect(view.knobsClipId() == std::optional<uint32_t>{f.clip->id()}).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("makes no take for a mod-button press while the arrangement records", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		*f.instrument->toModControllable()->getModKnobMode() = 0;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		{
			ArrangementRecording recording{f};
			view.modButtonAction(2, true);
			view.modButtonAction(2, false);
		}
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged);
		expect(f.instrument->getActiveClip() == f.clip || f.instrument->getActiveClip() == nullptr).to_be_true();
		*f.instrument->toModControllable()->getModKnobMode() = 0;
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("writes an arranger row's backed-up params when View turns a knob on a row with no clip", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		ParamManagerForTimeline params;
		auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		synth->setupAsDefaultSynth(&params);
		f.song->live.param_backup.backUp(*synth, nullptr, &params, true);
		f.song->addOutput(synth);
		const spec::SongFixture row{.song = f.song, .clip = nullptr, .instrument = synth};
		*synth->getModKnobMode() = 0;
		view.setActiveModControllableWithoutTimelineCounter(synth,
		                                                    f.song->live.param_backup.getPreferablyWithClip(*synth, nullptr));
		const std::optional<int32_t> before = panOn(row, std::nullopt);
		view.modEncoderAction(0, 7);
		expect(before.has_value()).to_be_true();
		expect(panOn(row, std::nullopt) != before).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("stays on the session clip after the arrangement stops recording", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		*f.instrument->toModControllable()->getModKnobMode() = 0;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		{
			ArrangementRecording recording{f};
			view.modEncoderAction(0, 3);
		}
		expect(view.knobsClipId() == std::optional<uint32_t>{f.clip->id()}).to_be_true();
		const std::optional<int32_t> session_before = panOn(f, f.clip->id());
		view.modEncoderAction(0, 3);
		expect(panOn(f, f.clip->id()) != session_before).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("reads the knobs' mode from the records: the song's, a track's, a kit row's kit's, none for a CV track", _ {
		spec::SongFixture f = spec::make_kit_song();
		currentSong = f.song;
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		static_cast<Kit*>(f.instrument)->selectedDrum = drum;
		f.clip->affectEntire = false;
		f.song->project.mod_knob_mode = 3;
		f.song->project.output_structure.find(*f.instrument->id())->mod_knob_mode = 5;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		expect(view.knobMode()).to_equal(5);
		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		expect(view.knobMode()).to_equal(3);
		view.releaseActiveModControllable();
		expect(view.knobMode()).to_equal(-1);

		spec::SongFixture cv = spec::make_cv_song();
		currentSong = cv.song;
		view.setActiveModControllableTimelineCounter(cv.clip, false);
		expect(view.knobMode()).to_equal(-1);
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("ends the knobs' stutter when a clip press ends on the session, and when the arranger leaves a sub-mode", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const project::ParamAddress owner = *project::knob_owner(view.knobFocus());

		enterUIMode(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW);
		(void)deluge::bridge::stutterAt(*f.song, owner, true);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
		(void)sessionView.padAction(kDisplayWidth - 1, kDisplayHeight - 1, 0);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
		exitUIMode(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW);

		enterUIMode(UI_MODE_HOLDING_ARRANGEMENT_ROW);
		(void)deluge::bridge::stutterAt(*f.song, owner, true);
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
		arrangerView.exitSubModeWithoutAction();
		expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
		currentUIMode = UI_MODE_NONE;
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("follows a held arranger row with no clip to its backed-up params", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		ParamManagerForTimeline params;
		auto* row = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		row->setupAsDefaultSynth(&params);
		f.song->live.param_backup.backUp(*row, nullptr, &params, true);
		f.song->addOutput(row);
		currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION;
		arrangerView.yPressedEffective = 2;
		Output* const saved = arrangerView.outputsOnScreen[2];
		arrangerView.outputsOnScreen[2] = row;
		arrangerView.refresh_held_row_mod_target();
		const project::KnobFocus want{.owner = project::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, *row->id()}}};
		expect(view.knobFocus().owner == want.owner).to_be_true();
		expect(View::KnobStack{view}.owner() == row->toModControllable()).to_be_true();
		arrangerView.outputsOnScreen[2] = saved;
		currentUIMode = UI_MODE_NONE;
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("keeps the compressor view in the focus: pushed into, cycled, kept across a re-point, fresh for a new song", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		f.song->persistentView.affectEntire = true;
		*f.song->live.global_effectable.getModKnobMode() = 4;
		f.song->project.mod_knob_mode = 4;
		view.setActiveModControllableTimelineCounter(f.song, false);
		view.knobFocus().comp = {};
		view.modEncoderButtonAction(1, true);
		view.modEncoderButtonAction(1, false);
		expect(view.knobFocus().comp.editing).to_be_true();
		view.modEncoderButtonAction(0, true);
		view.modEncoderButtonAction(0, false);
		view.modEncoderButtonAction(0, true);
		view.modEncoderButtonAction(0, false);
		expect(static_cast<int>(view.knobFocus().comp.param)).to_equal(2);

		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setActiveModControllableTimelineCounter(f.song, false);
		expect(view.knobFocus().comp == project::CompView{.editing = true, .param = 2}).to_be_true();
		expect(view.knobMode() == 4 && view.knobFocus().comp.editing).to_be_true(); // the gain-reduction meter's ask

		spec::SongFixture next = spec::make_synth_song();
		currentSong = next.song;
		next.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(next.song, false);
		expect(view.knobFocus().comp == project::CompView{}).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("copies a knob's automation and pastes it into another clip's knob by address, as one undo step", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture a = spec::make_synth_song();
		spec::SongFixture b = add_synth_clip(a);
		currentSong = a.song;
		*a.instrument->toModControllable()->getModKnobMode() = 0;
		*b.instrument->toModControllable()->getModKnobMode() = 0;
		const auto pan_of = [](const spec::SongFixture& f) {
			return project::ParamAddress{.owner = project::OwnerId::instrument(*f.instrument->id()),
			                             .space = project::ParamSpace::ENGINE, .id = deluge::instrument::param::kPan,
			                             .clip_id = f.clip->id()};
		};
		const auto names = [](const std::optional<project::ParamAddress>& got, const project::ParamAddress& want) {
			return got && got->owner.kind == want.owner.kind && got->owner.id == want.owner.id
			       && got->space == want.space && got->id == want.id && got->clip_id == want.clip_id;
		};
		const auto automated = [&](const spec::SongFixture& f) {
			const deluge::project::DocAutoParam* param = a.song->project.resolveParam(pan_of(f));
			return param != nullptr && !param->nodes.empty();
		};
		deluge::edits::Context ctx{*a.song, a.song->project};
		deluge::edits::SetParamValue seed{pan_of(a), 1 << 28, 0, 48};
		expect(deluge::edits::apply(ctx, seed) == Error::NONE).to_be_true();
		expect(automated(a)).to_be_true();
		expect(automated(b)).to_be_false();

		view.setActiveModControllableTimelineCounter(a.clip, false);
		expect(names(view.knobAddress(0), pan_of(a))).to_be_true();
		instrumentClipView.copyAutomation(0);
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		view.setActiveModControllableTimelineCounter(b.clip, false);
		expect(names(view.knobAddress(0), pan_of(b))).to_be_true();
		actionLogger.deleteAllLogs();
		instrumentClipView.pasteAutomation(0);
		expect(automated(b)).to_be_true();
		Action* action = actionLogger.firstAction[BEFORE];
		expect(action != nullptr && action->nextAction == nullptr).to_be_true();
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(automated(b)).to_be_false();
		actionLogger.deleteAllLogs();
		nullifyUIs();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("cycles the song's reverb preset and names it, from the song's reverb", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		const auto set = [&](size_t preset) {
			deluge::edits::songSettingFromMenu("reverb/roomSize", deluge::model::kReverbPresetRoomSize[preset]);
			deluge::edits::songSettingFromMenu("reverb/dampening", deluge::model::kReverbPresetDamping[preset]);
		};
		for (size_t preset = 0; preset < deluge::model::kNumReverbPresets; preset++) {
			set(preset);
			expect(deluge::model::reverbPresetOf(f.song->project)).to_equal(preset);
			deluge::model::cycleReverbPreset(*f.song);
			expect(deluge::model::reverbPresetOf(f.song->project)).to_equal((preset + 1) % deluge::model::kNumReverbPresets);
		}
		expect(std::string{deluge::model::reverbPresetName(0)}).to_equal(std::string{deluge::l10n::get(deluge::l10n::String::STRING_FOR_SMALL_ROOM_REVERB)});
		expect(std::string{deluge::model::reverbPresetName(2)}).to_equal(std::string{deluge::l10n::get(deluge::l10n::String::STRING_FOR_LARGE_ROOM_REVERB)});
		currentSong = nullptr;
	});

	it("keeps the compressor view the song's: a kit's knobs in mode 4 edit its reverb send, as the port's do", _ {
		spec::SongFixture f = spec::make_kit_song();
		currentSong = f.song;
		auto* kit = static_cast<Kit*>(f.instrument);
		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		view.knobFocus().comp = project::CompView{.editing = true, .param = 1};
		*kit->getModKnobMode() = 4;
		f.song->project.output_structure.find(*kit->id())->mod_knob_mode = 4;
		f.clip->affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		expect(view.knobFocus().comp.editing).to_be_true(); // the song's view, kept for the song
		const std::optional<project::ParamAddress> lower = view.knobAddress(0);
		expect(lower.has_value() && lower->id == deluge::global_effectable::param::kReverbAmount).to_be_true();
		const deluge::scenes::bind::KnobAssignment port =
		    deluge::scenes::bind::knob_assignment(f.song->project, view.knobFocus(), 0);
		const auto* address = std::get_if<project::ParamAddress>(&port.target);
		expect(address != nullptr && address->id == deluge::global_effectable::param::kReverbAmount).to_be_true();
		view.knobFocus().comp = {};
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("lets go of the knobs for a retired clip only while their song is the current one", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture elsewhere = spec::make_synth_song();
		currentSong = f.song;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		expect(elsewhere.clip->id() == f.clip->id()).to_be_true();
		currentSong = elsewhere.song; // a clip of another song retires, with the same id
		view.releaseKnobsIfOn(*elsewhere.clip);
		currentSong = f.song;
		expect(std::holds_alternative<project::SoundFocus>(focus().owner)).to_be_true();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("holds a step-edit region on the take the arrangement records the knobs' clip into", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		InstrumentClip* take = spec::detail::make_session_clip(f.song);
		take->beingRecordedFromClip = f.clip;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 0);
		expect(view.stepEditRegionHolds(*take)).to_be_true();
		take->beingRecordedFromClip = nullptr;
		expect(view.stepEditRegionHolds(*take)).to_be_false();
		view.setModRegion();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("writes a learned kit-row knob's region on the held clip", _ {
		spec::SongFixture f = spec::make_kit_song();
		currentSong = f.song;
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		static_cast<Kit*>(f.instrument)->selectedDrum = drum;
		f.clip->affectEntire = false;
		ParamDescriptor volume;
		volume.setToHaveParamOnly(params::LOCAL_VOLUME);
		(void)deluge::edits::learnKnobFor(*drum, volume, &MIDIDeviceManager::dinMIDIPorts, 75, 1);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, static_cast<int32_t>(*drum->id()));
		int32_t row_index = 0;
		(void)f.clip->getNoteRowForDrum(drum, &row_index);
		char memory[MODEL_STACK_MAX_SIZE];
		drum->offerReceivedCCToLearnedParamsForClip({project::ControlsAddress::Kind::DRUM, *drum->id()},
		                                            MIDIDeviceManager::dinMIDIPorts, 1, 75, 100,
		                                            setupModelStackWithTimelineCounter(memory, f.song, f.clip), row_index);
		const deluge::project::DocAutoParam* param = f.song->project.resolveParam(
		    {.owner = project::OwnerId::drum(*drum->id()), .space = project::ParamSpace::ENGINE,
		     .id = deluge::instrument::param::kVolume, .clip_id = f.clip->id()});
		expect(param != nullptr && !param->nodes.empty()).to_be_true();
		view.setModRegion();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("writes a MIDI track's input-channel CC as a region only while the knobs hold one on its clip", _ {
		spec::SongFixture f = spec::make_midi_song();
		currentSong = f.song;
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		const auto cc_automated = [&](int32_t cc) {
			const deluge::project::DocAutoParam* param = f.song->project.resolveParam(
			    {.owner = project::OwnerId::clip(f.clip->id()), .space = project::ParamSpace::MIDI_CC,
			     .id = deluge::engine::ParamId{static_cast<uint32_t>(cc)}});
			return param != nullptr && !param->nodes.empty();
		};
		char memory[MODEL_STACK_MAX_SIZE];
		midi->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip), PgmChangeSend::NEVER);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 0);
		midi->ccReceivedFromInputMIDIChannel(20, 100, setupModelStackWithTimelineCounter(memory, f.song, f.clip));
		expect(cc_automated(20)).to_be_true();
		view.setModRegion();
		midi->ccReceivedFromInputMIDIChannel(21, 100, setupModelStackWithTimelineCounter(memory, f.song, f.clip));
		expect(cc_automated(21)).to_be_false();
		view.releaseActiveModControllable();
		currentSong = nullptr;
	});

	it("takes an MPE value as the held note's edit on its row only", _ {
		spec::SongFixture f = spec::make_synth_song();
		currentSong = f.song;
		int32_t held_index = 0;
		NoteRow* held = f.clip->noteRows.insertNoteRowAtY(60, &held_index);
		int32_t other_index = 0;
		NoteRow* other = f.clip->noteRows.insertNoteRowAtY(61, &other_index);
		expect(held != nullptr && other != nullptr).to_be_true();
		if (held != nullptr && other != nullptr) {
			view.setActiveModControllableTimelineCounter(f.clip, false);
			char memory[MODEL_STACK_MAX_SIZE];
			{
				// The note editor holds the region with the row's id as its stack names it.
				int32_t at = 0;
				NoteRow* row = f.clip->getNoteRowForYNote(60, &at);
				view.setModRegion(96, 48,
				                  setupModelStackWithTimelineCounter(memory, f.song, f.clip)->addNoteRow(at, row)->noteRowId);
			}
			// Pressure (dimension 2) is what the note editor keeps of a held note's MPE.
			instrumentClipView.mpeMostRecentPressure = 0;
			int32_t index = 0;
			NoteRow* row = f.clip->getNoteRowForYNote(61, &index);
			(void)row->recordPolyphonicExpressionEvent(
			    setupModelStackWithTimelineCounter(memory, f.song, f.clip)->addNoteRow(index, row), 1 << 28, 2, false);
			expect(instrumentClipView.mpeMostRecentPressure).to_equal(int16_t{0});
			row = f.clip->getNoteRowForYNote(60, &index);
			(void)row->recordPolyphonicExpressionEvent(
			    setupModelStackWithTimelineCounter(memory, f.song, f.clip)->addNoteRow(index, row), 1 << 28, 2, false);
			expect(instrumentClipView.mpeMostRecentPressure).to_equal(int16_t{(1 << 28) >> 16});
			instrumentClipView.mpeMostRecentPressure = 0;
			view.setModRegion();
			view.releaseActiveModControllable();
		}
		currentSong = nullptr;
	});
});
// clang-format on

CPPSPEC_SPEC(view_focus)
