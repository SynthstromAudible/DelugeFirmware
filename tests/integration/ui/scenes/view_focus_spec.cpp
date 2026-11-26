// Legacy View's gold-knob focus, as addresses: every setter shape names the owner its stack names (a synth, a kit row,
// a kit's chain, an audio track, the song, a MIDI track, nothing for a CV track or a release, an arranger row's
// backup with no clip), and a setter leaves the focus's own view state (the region, the compressor view, the VU meter)
// as it was.

#include "board_controls.hpp"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/root_knobs.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/knob_focus_from_stack.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"

#include "effects/reverb_presets.h"
#include "gui/l10n/effect_names.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "io/midi/learned_dispatch.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_device_manager.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/edits/set_mod_knob_cc.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/edits/set_param_value.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/reverb_presets.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "ui/controls/knob_focus.h"
#include "ui/scenes/bind/knob_assignment.h"

#include "support/printers/knob_focus_printer.h"
#include "support/printers/param_id_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <array>
#include <limits>
#include <optional>
#include <string>

using namespace deluge;

/// The host BSP's shadow of the knob indicator columns, four segment brightnesses each; declared rather than included
/// because `host_script.h` is not on this target's include path.
extern "C" const uint8_t* host_script_ring_shadow(uint8_t which);
/// The rows `uiNeedsRendering` has asked for since `clearPendingUIRendering`; `gui/ui/ui.cpp`'s, which no header
/// declares.
extern uint32_t whichMainRowsNeedRendering;
extern uint32_t whichSideRowsNeedRendering;

namespace {

namespace project = deluge::project;
namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;

/// @brief View's focus owner now.
const ui::controls::KnobFocus& focus() {
	return view.knobFocus();
}

/// @brief The owner stack of View's knob focus, resolved in @p memory: none unless the focus is on the current song.
/// @param memory A model-stack buffer.
ModelStackWithThreeMainThings* view_knob_stack(void* memory) {
	return view.knobOwnerStack(memory);
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

/// @brief Make @p f's clip its instrument's active clip, as a song's own load does: the learned dispatch offers a CC to
///        the outputs that have one.
void activate(const spec::SongFixture& f) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	(void)f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip), PgmChangeSend::NEVER);
}

/// @brief Learn CC 74 on channel 1 of DIN to @p f's synth's volume, and send it @p value through the learned dispatch,
///        @p f's clip active.
void learnAndSendVolume(const spec::SongFixture& f, uint8_t value) {
	auto& sound = *static_cast<SoundInstrument*>(f.instrument);
	const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
	if (f.song->project.mappings.params_of({project::ControlsAddress::Kind::OUTPUT, *f.instrument->id()}).empty()) {
		(void)deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1);
	}
	activate(f);
	deluge::midi::refreshMappingIndex();
	deluge::midi::learnedCcToOutput(*f.instrument, MIDIDeviceManager::dinMIDIPorts, 1, 74, value);
}

/// @brief Is @p f's clip's synth volume automated?
bool volumeAutomated(const spec::SongFixture& f) {
	const deluge::project::DocAutoParam* param =
	    f.song->project.resolveParam({.owner = project::OwnerId::instrument(*f.instrument->id()),
	                                  .space = project::ParamSpace::ENGINE,
	                                  .id = deluge::instruments::classic::param::kVolume,
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
	void displayNotification(std::string_view, std::optional<std::string_view>) override { notifications++; }
	int32_t notifications = 0;
};

/// @brief Knob @p knob's indicator column as the host's LEDs show it, four segment brightnesses.
std::array<uint8_t, 4> ring(uint8_t knob) {
	const uint8_t* shadow = host_script_ring_shadow(knob);
	return {shadow[0], shadow[1], shadow[2], shadow[3]};
}

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
	                                     .id = deluge::instruments::classic::param::kPan,
	                                     .clip_id = clip},
	                                    0xFFFFFFFF);
}

} // namespace

// clang-format off
describe view_focus("legacy View's gold-knob focus, as addresses", ${
	it("names a synth on its clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const ui::controls::KnobFocus want{.owner = ui::controls::SoundFocus{
		    .slot = {project::SlotAddress::Kind::OUTPUT, *f.instrument->id()}, .clip = project::ClipAddress{f.clip->id()}}};
		expect(focus().owner).to_equal(want.owner);
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("names a kit row with its note row and kit, or the kit's chain with affect-entire", _ {
		spec::SongFixture f = spec::make_kit_song();
		deluge::ui::scenes::spec::make_current(f.song);
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		auto* kit = static_cast<Kit*>(f.instrument);
		kit->selectedDrum = drum;
		f.clip->affectEntire = false;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const ui::controls::KnobFocus row{.owner = ui::controls::SoundFocus{
		    .slot = {project::SlotAddress::Kind::DRUM, *drum->id()},
		    .clip = project::ClipAddress{f.clip->id()},
		    .row = project::NoteRowAddress{f.clip->id(), true, static_cast<int32_t>(*drum->id())},
		    .kit = *kit->id()}};
		expect(focus().owner).to_equal(row.owner);

		f.clip->affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const ui::controls::KnobFocus chain{.owner = ui::controls::ChainFocus{
		    .chain = {project::ChainAddress::Kind::OUTPUT, *kit->id()},
		    .owner = ui::controls::ChainOwner::KIT,
		    .clip = project::ClipAddress{f.clip->id()}}};
		expect(focus().owner).to_equal(chain.owner);
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("names an audio track's chain on its clip", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		deluge::ui::scenes::spec::make_current(f.song);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const ui::controls::KnobFocus want{.owner = ui::controls::ChainFocus{
		    .chain = {project::ChainAddress::Kind::OUTPUT, *f.output->id()},
		    .owner = ui::controls::ChainOwner::AUDIO_TRACK,
		    .clip = project::ClipAddress{f.clip->id()}}};
		expect(focus().owner).to_equal(want.owner);
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("names the song, a MIDI track on its clip, an empty layout for a CV track, and nothing for a release", _ {
		spec::SongFixture song = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(song.song);
		song.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(song.song, false);
		expect(std::holds_alternative<ui::controls::SongFocus>(focus().owner)).to_be_true();
		view.releaseActiveModControllable();
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();

		spec::SongFixture midi = spec::make_midi_song();
		deluge::ui::scenes::spec::make_current(midi.song);
		view.setActiveModControllableTimelineCounter(midi.clip, false);
		const ui::controls::KnobFocus want{.owner = ui::controls::MidiFocus{
		    .track = project::TrackAddress{*midi.instrument->id()}, .clip = project::ClipAddress{midi.clip->id()}}};
		expect(focus().owner).to_equal(want.owner);

		spec::SongFixture cv = spec::make_cv_song();
		deluge::ui::scenes::spec::make_current(cv.song);
		view.setActiveModControllableTimelineCounter(cv.clip, false);
		const ui::controls::KnobFocus empty{.owner = ui::controls::NoControlsFocus{
		    .kind = ui::controls::NoControlsFocus::Kind::CV_TRACK,
		    .track = project::TrackAddress{*cv.instrument->id()},
		    .clip_id = cv.clip->id()}};
		expect(focus().owner).to_equal(empty.owner);
		expect(ui::controls::has_no_knobs(focus())).to_be_true();
		expect(ui::controls::knob_owner(focus()).has_value()).to_be_false();
		expect(ui::controls::knob_mode_owner(focus()).has_value()).to_be_false();
		view.releaseActiveModControllable();
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("names an empty layout, with its kit and drum, for a MIDI drum and a gate drum the stack names", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* midi = spec::add_drum<MIDIDrum>(f);
		GateDrum* gate = spec::add_drum<GateDrum>(f);
		const uint32_t kit = *f.instrument->id();
		// The clip view's own stack names no controllable for these drums, so the stack is made by hand.
		const auto focus_on = [&](Drum& drum, bool in_clip) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			setupModelStackWithSong(memory, f.song)
			    ->addTimelineCounter(in_clip ? static_cast<TimelineCounter*>(f.clip) : nullptr)
			    ->addOtherTwoThingsButNoNoteRow(deluge::model::owner_of(drum), nullptr);
			return deluge::gui::knobFocusFromStack(*f.song, *reinterpret_cast<ModelStackWithThreeMainThings*>(memory));
		};
		const ui::controls::KnobFocus midi_in_clip = focus_on(*midi, true);
		const ui::controls::KnobFocus want_midi{.owner = ui::controls::NoControlsFocus{
		    .kind = ui::controls::NoControlsFocus::Kind::MIDI_DRUM,
		    .track = project::TrackAddress{kit},
		    .drum = project::KitRowAddress{kit, *midi->id()},
		    .clip_id = f.clip->id()}};
		expect(midi_in_clip.owner).to_equal(want_midi.owner);
		const ui::controls::KnobFocus want_gate{.owner = ui::controls::NoControlsFocus{
		    .kind = ui::controls::NoControlsFocus::Kind::GATE_DRUM,
		    .track = project::TrackAddress{kit},
		    .drum = project::KitRowAddress{kit, *gate->id()}}};
		const ui::controls::KnobFocus gate_no_clip = focus_on(*gate, false);
		expect(gate_no_clip.owner).to_equal(want_gate.owner);
		for (const ui::controls::KnobFocus& found : {midi_in_clip, gate_no_clip}) {
			expect(ui::controls::has_no_knobs(found)).to_be_true();
			expect(ui::controls::knob_owner(found).has_value()).to_be_false();
			expect(ui::controls::knob_mode_owner(found).has_value()).to_be_false();
		}
	});

	it("names an arranger row's backup with no clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		ParamManagerForTimeline params;
		auto* row = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		row->setupAsDefaultSynth(&params);
		f.song->live.param_backup.backUp(*row, nullptr, &params, true);
		f.song->addOutput(row);
		view.setActiveModControllableWithoutTimelineCounter(row, f.song->live.param_backup.getPreferablyWithClip(*row, nullptr));
		const ui::controls::KnobFocus want{.owner = ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, *row->id()}}};
		expect(focus().owner).to_equal(want.owner);
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("keeps its own view state -- the region, the compressor view, the VU meter -- across a setter", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 0);
		view.knobFocus().comp = ui::controls::CompView{.editing = true, .param = 2};
		view.knobFocus().vu_meter = true;
		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		expect(focus().region).to_equal(ui::controls::ModRegion{.pos = 96, .length = 48});
		expect(focus().comp).to_equal(ui::controls::CompView{.editing = true, .param = 2});
		expect(focus().vu_meter).to_be_true();
		view.knobFocus().comp = {};
		view.knobFocus().vu_meter = false;
		view.setModRegion();
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("answers which clip and kit row the knobs are on, by id, in their own song only", _ {
		spec::SongFixture f = spec::make_kit_song();
		deluge::ui::scenes::spec::make_current(f.song);
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
		deluge::ui::scenes::spec::make_current(f.song);
		expect(view.knobsOnClip(*elsewhere.song, *elsewhere.clip)).to_be_false();

		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		expect(view.knobsOnClip(*f.song, *f.clip)).to_be_false();
		expect(view.knobsOnDrum(*f.song, *drum)).to_be_false();
		expect(view.knobsOnClipContext()).to_be_false();
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("keeps a CV track in the clip context though its knobs move nothing", _ {
		spec::SongFixture cv = spec::make_cv_song();
		deluge::ui::scenes::spec::make_current(cv.song);
		view.setActiveModControllableTimelineCounter(cv.clip, false);
		expect(view.knobsOnClipContext()).to_be_true();
		view.releaseActiveModControllable();
		expect(view.knobsOnClipContext()).to_be_false();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("holds a step-edit region on the clip and row the pad names, and nowhere else", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture b = add_synth_clip(f);
		deluge::ui::scenes::spec::make_current(f.song);
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
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("writes a learned knob's region only on the clip whose note is held", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture b = add_synth_clip(f);
		deluge::ui::scenes::spec::make_current(f.song);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 0);
		learnAndSendVolume(b, 100);
		expect(volumeAutomated(b)).to_be_false();
		learnAndSendVolume(f, 100);
		expect(volumeAutomated(f)).to_be_true();
		view.setModRegion();
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("lets go of the knobs when their clip or song retires, and only then", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture b = add_synth_clip(f);
		deluge::ui::scenes::spec::make_current(f.song);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.releaseKnobsIfOn(*b.clip);
		expect(std::holds_alternative<ui::controls::SoundFocus>(focus().owner)).to_be_true();
		view.releaseKnobsIfOn(*f.clip);
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();
		alignas(ModelStackWithAutoParam) char owner_memory[MODEL_STACK_MAX_SIZE];
		expect(view_knob_stack(owner_memory)).to_be_null();

		view.setActiveModControllableTimelineCounter(f.clip, false);
		spec::SongFixture elsewhere = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		view.releaseKnobsIfOnSong(*elsewhere.song);
		expect(std::holds_alternative<ui::controls::SoundFocus>(focus().owner)).to_be_true();
		view.releaseKnobsIfOnSong(*f.song);
		expect(std::holds_alternative<std::monostate>(focus().owner)).to_be_true();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("clones once when View turns a knob while the arrangement records, and leaves the session clip", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*f.instrument->id()}, 0);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const std::optional<int32_t> session_before = panOn(f, f.clip->id());
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		Clip* recorded = nullptr;
		{
			ArrangementRecording recording{f};
			deluge::gui::ui::root_knobs::turn(0, 5);
			deluge::gui::ui::root_knobs::turn(0, 5);
			recorded = f.instrument->getActiveClip();
		}
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		expect(recorded).not_().to_be_null();
		if (recorded != nullptr) {
			expect(recorded).not_().to_equal(f.clip);
			expect(recorded->beingRecordedFromClip).to_equal(f.clip);
		}
		expect(panOn(f, f.clip->id())).to_equal(session_before);
		if (recorded != nullptr && recorded != f.clip) {
			expect(panOn(f, recorded->id())).not_().to_equal(session_before);
		}
		expect(view.knobsClipId()).to_equal(std::optional<uint32_t>{f.clip->id()});
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("makes no take for a mod-button press while the arrangement records", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*f.instrument->id()}, 0);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		{
			ArrangementRecording recording{f};
			deluge::gui::ui::root_knobs::mod_button(2, true);
			deluge::gui::ui::root_knobs::mod_button(2, false);
		}
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged);
		expect(f.instrument->getActiveClip()).to_satisfy([&](auto* clip) { return clip == f.clip || clip == nullptr; });
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*f.instrument->id()}, 0);
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("writes an arranger row's backed-up params when View turns a knob on a row with no clip", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		ParamManagerForTimeline params;
		auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		synth->setupAsDefaultSynth(&params);
		f.song->live.param_backup.backUp(*synth, nullptr, &params, true);
		f.song->addOutput(synth);
		const spec::SongFixture row{.song = f.song, .clip = nullptr, .instrument = synth};
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*synth->id()}, 0);
		view.setActiveModControllableWithoutTimelineCounter(synth,
		                                                    f.song->live.param_backup.getPreferablyWithClip(*synth, nullptr));
		const std::optional<int32_t> before = panOn(row, std::nullopt);
		deluge::gui::ui::root_knobs::turn(0, 7);
		expect(before.has_value()).to_be_true();
		expect(panOn(row, std::nullopt)).not_().to_equal(before);
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("stays on the session clip after the arrangement stops recording", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*f.instrument->id()}, 0);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		{
			ArrangementRecording recording{f};
			deluge::gui::ui::root_knobs::turn(0, 3);
		}
		expect(view.knobsClipId()).to_equal(std::optional<uint32_t>{f.clip->id()});
		const std::optional<int32_t> session_before = panOn(f, f.clip->id());
		deluge::gui::ui::root_knobs::turn(0, 3);
		expect(panOn(f, f.clip->id())).not_().to_equal(session_before);
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("reads the knobs' mode from the records: the song's, a track's, a kit row's kit's, none for a CV track", _ {
		spec::SongFixture f = spec::make_kit_song();
		deluge::ui::scenes::spec::make_current(f.song);
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
		deluge::ui::scenes::spec::make_current(cv.song);
		view.setActiveModControllableTimelineCounter(cv.clip, false);
		expect(view.knobMode()).to_equal(-1);
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("ends the knobs' stutter when a clip press ends on the session, and when the arranger leaves a sub-mode", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const project::ParamAddress owner = *ui::controls::knob_owner(view.knobFocus());

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
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("follows a held arranger row with no clip to its backed-up params", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
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
		const ui::controls::KnobFocus want{.owner = ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, *row->id()}}};
		expect(view.knobFocus().owner).to_equal(want.owner);
		alignas(ModelStackWithAutoParam) char owner_memory[MODEL_STACK_MAX_SIZE];
		const ModelStackWithThreeMainThings* owner = view_knob_stack(owner_memory);
		expect(owner).not_().to_be_null("the focus's owner stack");
		if (owner != nullptr) {
			expect(std::get<Sound*>(owner->owner)).to_equal(std::get<Sound*>(deluge::model::owner_of(*row)));
		}
		arrangerView.outputsOnScreen[2] = saved;
		currentUIMode = UI_MODE_NONE;
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("keeps the compressor view in the focus: pushed into, cycled, kept across a re-point, fresh for a new song", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		f.song->persistentView.affectEntire = true;
		(void)deluge::edits::modKnobModeFor(project::SongAddress{}, 4);
		view.setActiveModControllableTimelineCounter(f.song, false);
		view.knobFocus().comp = {};
		deluge::gui::ui::root_knobs::push(1, true);
		deluge::gui::ui::root_knobs::push(1, false);
		expect(view.knobFocus().comp.editing).to_be_true();
		deluge::gui::ui::root_knobs::push(0, true);
		deluge::gui::ui::root_knobs::push(0, false);
		deluge::gui::ui::root_knobs::push(0, true);
		deluge::gui::ui::root_knobs::push(0, false);
		expect(static_cast<int>(view.knobFocus().comp.param)).to_equal(2);

		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setActiveModControllableTimelineCounter(f.song, false);
		expect(view.knobFocus().comp).to_equal(ui::controls::CompView{.editing = true, .param = 2});
		expect(view.knobMode()).to_equal(4);
		expect(view.knobFocus().comp.editing).to_be_true(); // the gain-reduction meter's ask

		spec::SongFixture next = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(next.song);
		next.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(next.song, false);
		expect(view.knobFocus().comp).to_equal(ui::controls::CompView{});
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("copies a knob's automation and pastes it into another clip's knob by address, as one undo step", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture a = spec::make_synth_song();
		spec::SongFixture b = add_synth_clip(a);
		deluge::ui::scenes::spec::make_current(a.song);
		// Through the record, which the knobs read the mode from.
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*a.instrument->id()}, 0);
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*b.instrument->id()}, 0);
		const auto pan_of = [](const spec::SongFixture& f) {
			return project::ParamAddress{.owner = project::OwnerId::instrument(*f.instrument->id()),
			                             .space = project::ParamSpace::ENGINE, .id = deluge::instruments::classic::param::kPan,
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
		expect(deluge::edits::apply(ctx, seed)).to_equal(Error::NONE);
		expect(automated(a)).to_be_true();
		expect(automated(b)).to_be_false();

		view.setActiveModControllableTimelineCounter(a.clip, false);
		expect(names(deluge::gui::ui::root_knobs::knob_address(0), pan_of(a))).to_be_true();
		instrumentClipView.copyAutomation(0);
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		view.setActiveModControllableTimelineCounter(b.clip, false);
		expect(names(deluge::gui::ui::root_knobs::knob_address(0), pan_of(b))).to_be_true();
		actionLogger.deleteAllLogs();
		instrumentClipView.pasteAutomation(0);
		expect(automated(b)).to_be_true();
		Action* action = actionLogger.firstAction[BEFORE];
		expect(action).not_().to_be_null();
		if (action != nullptr) {
			expect(action->nextAction).to_be_null();
		}
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(automated(b)).to_be_false();
		actionLogger.deleteAllLogs();
		nullifyUIs();
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("names no address for a MIDI CC lane not made yet, where an engine param always has one", _ {
		// The root views' copy and paste of a knob's automation reads `root_knobs::knob_address`: no address is a paste
		// the view refuses with CANT_PASTE. A param an owner holds is always addressed; a MIDI lane only once made.
		StubDisplay stub;
		display = &stub;
		spec::SongFixture synth = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(synth.song);
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*synth.instrument->id()}, 0);
		view.setActiveModControllableTimelineCounter(synth.clip, false);
		expect(panOn(synth, synth.clip->id()).has_value()).to_be_true(); // pan has no automation: still a param
		const std::optional<project::ParamAddress> engine = deluge::gui::ui::root_knobs::knob_address(0);
		expect(engine.has_value()).to_be_true("the lower knob's address");
		if (engine.has_value()) {
			expect(engine->space).to_equal(project::ParamSpace::ENGINE);
			expect(engine->id).to_equal(deluge::instruments::classic::param::kPan);
		}
		view.releaseActiveModControllable();

		spec::SongFixture midi = spec::make_midi_song();
		deluge::ui::scenes::spec::make_current(midi.song);
		const project::TrackAddress track{*midi.instrument->id()};
		(void)deluge::edits::modKnobModeFor(track, 0);
		(void)deluge::edits::modKnobCCFor(track, 0, {.kind = project::ModKnobCc::Kind::NUMBER, .number = 74});
		view.setActiveModControllableTimelineCounter(midi.clip, false);
		const project::ParamAddress cc74{.owner = project::OwnerId::clip(midi.clip->id()),
		                                 .space = project::ParamSpace::MIDI_CC,
		                                 .id = deluge::plugin::ParamId{74}};
		expect(deluge::bridge::paramValueAt(*midi.song, cc74, 0xFFFFFFFF).has_value()).to_be_false();
		expect(deluge::gui::ui::root_knobs::knob_address(0).has_value()).to_be_false();

		// A turn makes the lane, and the knob then names it.
		deluge::gui::ui::root_knobs::turn(0, 5);
		expect(deluge::bridge::paramValueAt(*midi.song, cc74, 0xFFFFFFFF).has_value()).to_be_true();
		const std::optional<project::ParamAddress> made = deluge::gui::ui::root_knobs::knob_address(0);
		expect(made.has_value()).to_be_true("the lower knob's address");
		if (made.has_value()) {
			expect(made->space).to_equal(project::ParamSpace::MIDI_CC);
			expect(made->id.value).to_equal(74u);
		}
		view.releaseActiveModControllable();
		display = nullptr;
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("shows knobs of a song that is not the current one as pointing at nothing: indicators clear, a turn writes nothing", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture elsewhere = spec::make_synth_song();
		for (const spec::SongFixture* side : {&f, &elsewhere}) {
			deluge::ui::scenes::spec::make_current(side->song);
			(void)deluge::edits::modKnobModeFor(project::TrackAddress{*side->instrument->id()}, 0);
		}
		deluge::ui::scenes::spec::make_current(f.song);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		expect(view.knobsOnCurrentSong()).to_be_true();

		// A level on both columns that a clear would take away, then the current song changes under the knobs.
		for (const uint8_t knob : {0, 1}) {
			indicator_leds::setKnobIndicatorLevel(knob, 200, false);
			expect(static_cast<int32_t>(host_script_ring_shadow(knob)[0])).not_().to_equal(0);
		}
		const std::optional<int32_t> pan_before = panOn(f, f.clip->id());
		const std::optional<int32_t> other_before = panOn(elsewhere, elsewhere.clip->id());
		deluge::ui::scenes::spec::make_current(elsewhere.song);
		expect(view.knobsOnCurrentSong()).to_be_false();

		deluge::gui::ui::root_knobs::refresh_indicators();
		for (const uint8_t knob : {0, 1}) {
			for (int32_t segment = 0; segment < 4; segment++) {
				expect(static_cast<int32_t>(host_script_ring_shadow(knob)[segment])).to_equal(0);
			}
		}
		deluge::gui::ui::root_knobs::turn(0, 5);
		expect(panOn(elsewhere, elsewhere.clip->id())).to_equal(other_before);
		deluge::ui::scenes::spec::make_current(f.song);
		expect(panOn(f, f.clip->id())).to_equal(pan_before);
		view.releaseActiveModControllable();
		display = nullptr;
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("cycles the song's reverb preset and names it, from the song's reverb", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		const auto set = [&](size_t preset) {
			deluge::edits::songSettingFromMenu("reverb/roomSize", deluge::effects::kReverbPresets[preset].room_size);
			deluge::edits::songSettingFromMenu("reverb/dampening", deluge::effects::kReverbPresets[preset].damping);
		};
		for (size_t preset = 0; preset < deluge::effects::kReverbPresets.size(); preset++) {
			set(preset);
			expect(deluge::model::reverbPresetOf(f.song->project)).to_equal(preset);
			deluge::model::cycleReverbPreset(*f.song);
			expect(deluge::model::reverbPresetOf(f.song->project)).to_equal((preset + 1) % deluge::effects::kReverbPresets.size());
		}
		expect(std::string{deluge::l10n::reverb_preset_name(0)}).to_equal(std::string{deluge::l10n::get(deluge::l10n::String::STRING_FOR_SMALL_ROOM_REVERB)});
		expect(std::string{deluge::l10n::reverb_preset_name(2)}).to_equal(std::string{deluge::l10n::get(deluge::l10n::String::STRING_FOR_LARGE_ROOM_REVERB)});
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("keeps the compressor view the song's: a kit's knobs in mode 4 edit its reverb send, as the port's do", _ {
		spec::SongFixture f = spec::make_kit_song();
		deluge::ui::scenes::spec::make_current(f.song);
		auto* kit = static_cast<Kit*>(f.instrument);
		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		view.knobFocus().comp = ui::controls::CompView{.editing = true, .param = 1};
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*kit->id()}, 4);
		f.clip->affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		expect(view.knobFocus().comp.editing).to_be_true(); // the song's view, kept for the song
		const std::optional<project::ParamAddress> lower = deluge::gui::ui::root_knobs::knob_address(0);
		expect(lower.has_value()).to_be_true();
		if (lower.has_value()) {
			expect(lower->id).to_equal(deluge::host::chains::param::kReverbAmount);
		}
		const deluge::ui::scenes::bind::KnobAssignment port =
		    deluge::ui::scenes::bind::knob_assignment(f.song->project, view.knobFocus(), 0, kDelugeControls);
		const auto* address = std::get_if<project::ParamAddress>(&port.target);
		expect(address).not_().to_be_null();
		if (address != nullptr) {
			expect(address->id).to_equal(deluge::host::chains::param::kReverbAmount);
		}
		view.knobFocus().comp = {};
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("lets go of the knobs for a retired clip only while their song is the current one", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture elsewhere = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		expect(elsewhere.clip->id()).to_equal(f.clip->id());
		deluge::ui::scenes::spec::make_current(elsewhere.song); // a clip of another song retires, with the same id
		view.releaseKnobsIfOn(*elsewhere.clip);
		deluge::ui::scenes::spec::make_current(f.song);
		expect(std::holds_alternative<ui::controls::SoundFocus>(focus().owner)).to_be_true();
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("holds a step-edit region on the take the arrangement records the knobs' clip into", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		InstrumentClip* take = spec::detail::make_session_clip(f.song);
		take->beingRecordedFromClip = f.clip;
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 0);
		expect(view.stepEditRegionHolds(*take)).to_be_true();
		take->beingRecordedFromClip = nullptr;
		expect(view.stepEditRegionHolds(*take)).to_be_false();
		view.setModRegion();
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("writes a learned kit-row knob's region on the held clip", _ {
		spec::SongFixture f = spec::make_kit_song();
		deluge::ui::scenes::spec::make_current(f.song);
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		static_cast<Kit*>(f.instrument)->selectedDrum = drum;
		f.clip->affectEntire = false;
		const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
		(void)deluge::edits::learnKnobFor(drum, volume, &MIDIDeviceManager::dinMIDIPorts, 75, 1);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, static_cast<int32_t>(*drum->id()));
		activate(f);
		deluge::midi::refreshMappingIndex();
		deluge::midi::learnedCcToOutput(*f.instrument, MIDIDeviceManager::dinMIDIPorts, 1, 75, 100);
		const deluge::project::DocAutoParam* param = f.song->project.resolveParam(
		    {.owner = project::OwnerId::drum(*drum->id()), .space = project::ParamSpace::ENGINE,
		     .id = deluge::instruments::classic::param::kVolume, .clip_id = f.clip->id()});
		expect(param).not_().to_be_null();
		if (param != nullptr) {
			expect(param->nodes.empty()).to_be_false();
		}
		view.setModRegion();
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("writes a MIDI track's input-channel CC as a region only while the knobs hold one on its clip", _ {
		spec::SongFixture f = spec::make_midi_song();
		deluge::ui::scenes::spec::make_current(f.song);
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		const auto cc_automated = [&](int32_t cc) {
			const deluge::project::DocAutoParam* param = f.song->project.resolveParam(
			    {.owner = project::OwnerId::clip(f.clip->id()), .space = project::ParamSpace::MIDI_CC,
			     .id = deluge::plugin::ParamId{static_cast<uint32_t>(cc)}});
			return param != nullptr && !param->nodes.empty();
		};
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		midi->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip), PgmChangeSend::NEVER);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		view.setModRegion(96, 48, 0);
		midi->ccReceivedFromInputMIDIChannel(20, 100, setupModelStackWithTimelineCounter(memory, f.song, f.clip));
		expect(cc_automated(20)).to_be_true();
		view.setModRegion();
		midi->ccReceivedFromInputMIDIChannel(21, 100, setupModelStackWithTimelineCounter(memory, f.song, f.clip));
		expect(cc_automated(21)).to_be_false();
		view.releaseActiveModControllable();
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("takes an MPE value as the held note's edit on its row only", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		int32_t held_index = 0;
		NoteRow* held = f.clip->noteRows.insertNoteRowAtY(60, &held_index);
		int32_t other_index = 0;
		NoteRow* other = f.clip->noteRows.insertNoteRowAtY(61, &other_index);
		expect(held).not_().to_be_null();
		expect(other).not_().to_be_null();
		if (held != nullptr && other != nullptr) {
			view.setActiveModControllableTimelineCounter(f.clip, false);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
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
		deluge::ui::scenes::spec::make_current(nullptr);
	});
	it("shows no value popup for a root turn while a song loads, and still writes the turn", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*f.instrument->id()}, 0);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		setRootUILowLevel(&sessionView);
		deluge::gui::ui::root_knobs::turn(0, 5);
		expect(stub.notifications).to_equal(1);

		setRootUILowLevel(&loadSongUI); // the load UI is current: `isLoadingSong`
		const std::optional<int32_t> before = panOn(f, f.clip->id());
		deluge::gui::ui::root_knobs::turn(0, 5);
		expect(stub.notifications).to_equal(1);
		expect(panOn(f, f.clip->id())).not_().to_equal(before);
		nullifyUIs();
		view.releaseActiveModControllable();
		display = nullptr;
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("shows the song compressor knob at the top of its travel as 128 on a root turn", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		f.song->persistentView.affectEntire = true;
		(void)deluge::edits::modKnobModeFor(project::SongAddress{}, 4);
		view.setActiveModControllableTimelineCounter(f.song, false);
		view.knobFocus().comp = {};
		deluge::gui::ui::root_knobs::push(1, true); // into the compressor view: the upper knob is the threshold
		deluge::gui::ui::root_knobs::push(1, false);
		expect(view.knobFocus().comp.editing).to_be_true();
		for (int32_t i = 0; i < 4; i++) {
			deluge::gui::ui::root_knobs::turn(1, 40);
		}
		const std::array<uint8_t, 4> top = ring(1);
		indicator_leds::setKnobIndicatorLevel(1, 127, false);
		const std::array<uint8_t, 4> below = ring(1);
		indicator_leds::setKnobIndicatorLevel(1, 128, false);
		expect(ring(1)).to_equal(top);
		expect(below).not_().to_equal(top);
		view.knobFocus().comp = {};
		view.releaseActiveModControllable();
		display = nullptr;
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("redraws the root's sidebar on a mod button only while the VU meter is drawn there", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		f.song->persistentView.affectEntire = true;
		(void)deluge::edits::modKnobModeFor(project::SongAddress{}, 0);
		view.setActiveModControllableTimelineCounter(f.song, false);
		setRootUILowLevel(&sessionView);

		clearPendingUIRendering();
		deluge::gui::ui::root_knobs::mod_button(1, true);
		deluge::gui::ui::root_knobs::mod_button(1, false);
		expect(whichSideRowsNeedRendering).to_equal(0u);

		(void)deluge::edits::modKnobModeFor(project::SongAddress{}, 0);
		view.knobFocus().vu_meter = true;
		RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
		expect(view.potentiallyRenderVUMeter(image)).to_be_true();
		expect(view.vuMeterRendered()).to_be_true();
		clearPendingUIRendering();
		deluge::gui::ui::root_knobs::mod_button(1, true);
		deluge::gui::ui::root_knobs::mod_button(1, false);
		expect(whichSideRowsNeedRendering).not_().to_equal(0u);
		expect(whichMainRowsNeedRendering).to_equal(0u);

		clearPendingUIRendering();
		view.knobFocus().vu_meter = false;
		nullifyUIs();
		view.releaseActiveModControllable();
		display = nullptr;
		deluge::ui::scenes::spec::make_current(nullptr);
	});

	it("repaints a timeline root's pads when a root turn changes a synth's note tails, and no other root's", _ {
		StubDisplay stub;
		display = &stub;
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::scenes::spec::make_current(f.song);
		// The third mod button's lower knob is envelope 1's release; with no sustain, tails hold only while the decay
		// is above the release.
		(void)deluge::edits::modKnobModeFor(project::TrackAddress{*f.instrument->id()}, 2);
		view.setActiveModControllableTimelineCounter(f.clip, false);
		const auto set_value = [&](int32_t id, int32_t value) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(
			    *f.song,
			    {.owner = project::OwnerId::instrument(*f.instrument->id()),
			     .space = project::ParamSpace::ENGINE,
			     .id = deluge::bridge::paramIdOf(params::Kind::PATCHED, id),
			     .clip_id = f.clip->id()},
			    memory);
			expect(stack).not_().to_be_null("the param's stack");
			if (stack != nullptr) {
				stack->autoParam->setCurrentValueWithNoReversionOrRecording(stack, value);
			}
		};
		// Turns the release up until it passes the decay, and answers how many turns asked for every main pad.
		const auto turn_past_decay = [&] {
			set_value(params::LOCAL_ENV_0_SUSTAIN, std::numeric_limits<int32_t>::min());
			set_value(params::LOCAL_ENV_0_DECAY, -(1 << 29));
			set_value(params::LOCAL_ENV_0_RELEASE, std::numeric_limits<int32_t>::min());
			int32_t repaints = 0;
			for (int32_t i = 0; i < 60; i++) {
				clearPendingUIRendering();
				deluge::gui::ui::root_knobs::turn(0, 1);
				repaints += whichMainRowsNeedRendering == 0xFFFFFFFF ? 1 : 0;
			}
			return repaints;
		};
		setRootUILowLevel(&sessionView);
		expect(turn_past_decay()).to_equal(1);
		setRootUILowLevel(&keyboardScreen);
		expect(turn_past_decay()).to_equal(0);
		clearPendingUIRendering();
		nullifyUIs();
		view.releaseActiveModControllable();
		display = nullptr;
		deluge::ui::scenes::spec::make_current(nullptr);
	});
});
// clang-format on

CPPSPEC_SPEC(view_focus)
