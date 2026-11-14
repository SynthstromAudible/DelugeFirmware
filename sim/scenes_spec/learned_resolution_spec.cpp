/// @file
/// @brief What a learned dispatch asks of the app, one member at a time: the write resolution `Resolve::LEARNED` (the
///        arrangement's take, a cable made, no owner marked edited, the undo step the legacy writes make), the sink's
///        learned members, and the facts a dispatch reads (the playing outputs, a kit's sound rows, the takeover's
///        inputs, the held step region, a param that does not exist yet).

#include "deluge-project/mapping_table.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "instruments/deluge/params.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_engine.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence_param_change.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/edits/set_param_value.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "scenes/menu/bind_host.h"
#include "storage/storage_manager.h"
#include "synth/modulation/cable.h"

#include <optional>
#include <string>
#include <vector>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace spec = deluge::scenes::spec;
namespace params = deluge::modulation::params;
namespace project = deluge::project;
namespace controls = deluge::controls;

using project::ControlsAddress;
using project::OwnerId;
using project::OwnerParamTarget;
using project::ParamAddress;
using project::ParamSpace;
using Resolve = deluge::edits::SetParamValue::Resolve;
using Kind = ControlsAddress::Kind;

namespace {

constexpr uint8_t kChannel = 0;
constexpr uint8_t kCc = 1;

/// @brief The depth of the cable from @p source onto patched param @p p, as the table names it.
project::ParamTarget depthOf(int32_t p, PatchSource source) {
	return project::engine_target(
	    deluge::bridge::controlTargetOf(
	        deluge::modulation::knobTargetOf(deluge::synth::CableTarget::depth_of(static_cast<uint8_t>(p), source)),
	        params::Kind::UNPATCHED_SOUND)
	        .value);
}

/// @brief The address of the filter's frequency or resonance on @p f's synth, in @p clip.
ParamAddress filterOf(const spec::SongFixture& f, deluge::plugin::ParamId id, const Clip& clip) {
	return ParamAddress{
	    .owner = OwnerId::instrument(*f.instrument->id()), .space = ParamSpace::ENGINE, .id = id, .clip_id = clip.id()};
}

/// @brief The address of a cable depth on @p f's synth, in @p clip.
ParamAddress cableOf(const spec::SongFixture& f, const Clip& clip) {
	const project::ParamTarget depth = depthOf(params::LOCAL_LPF_FREQ, PatchSource::LFO_GLOBAL_1);
	return ParamAddress{
	    .owner = OwnerId::instrument(*f.instrument->id()), .space = depth.space, .id = depth.id, .clip_id = clip.id()};
}

/// @brief Make @p clip its @p output's active clip, as a song's own load does.
void activate(Song& song, Output& output, Clip& clip) {
	alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
	(void)output.setActiveClip(setupModelStackWithSong(memory, &song)->addTimelineCounter(&clip), PgmChangeSend::NEVER);
}

/// @brief The lane's current value, or none where the song holds no such lane.
std::optional<int32_t> valueOf(Song& song, const ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
	return stack != nullptr && stack->autoParam != nullptr ? std::optional<int32_t>{stack->autoParam->getCurrentValue()}
	                                                       : std::nullopt;
}

/// @brief Apply @p value at @p address as a write resolved as @p resolve; whether the song took it.
bool write(Song& song, const ParamAddress& address, int32_t value, Resolve resolve) {
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::SetParamValue edit{address, value, 0, 0, resolve};
	return deluge::edits::apply(ctx, edit) == Error::NONE;
}

/// @brief A view as the root UI, so an undo step can open, for one example.
struct RootView {
	RootView() { changeRootUI(&sessionView); }
	~RootView() { nullifyUIs(); }
	RootView(const RootView&) = delete;
	RootView& operator=(const RootView&) = delete;
};

/// @brief The clock running, with @p mode recording, for as long as it lives.
struct Transport {
	RecordingMode saved_mode = playbackHandler.recording;
	decltype(playbackHandler.playbackState) saved_state = playbackHandler.playbackState;
	explicit Transport(RecordingMode mode) {
		playbackHandler.recording = mode;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
	}
	~Transport() {
		playbackHandler.recording = saved_mode;
		playbackHandler.playbackState = saved_state;
	}
	Transport(const Transport&) = delete;
	Transport& operator=(const Transport&) = delete;
};

/// @brief The arrangement recording @p f's clip at 0, for as long as it lives.
struct ArrangementRecording {
	Transport transport{RecordingMode::ARRANGEMENT};
	explicit ArrangementRecording(const spec::SongFixture& f) {
		f.clip->activeIfNoSolo = true;
		const int32_t at = f.instrument->clipInstances.insertSorted(0).value_or(-1);
		if (at >= 0) {
			f.instrument->clipInstances[at].clip = f.clip;
			f.instrument->clipInstances[at].length = f.clip->loopLength();
		}
	}
};

/// @brief The takeover mode for one example.
struct TakeoverSetting {
	MIDITakeoverMode saved = midiEngine.midiTakeover;
	explicit TakeoverSetting(MIDITakeoverMode mode) { midiEngine.midiTakeover = mode; }
	~TakeoverSetting() { midiEngine.midiTakeover = saved; }
	TakeoverSetting(const TakeoverSetting&) = delete;
	TakeoverSetting& operator=(const TakeoverSetting&) = delete;
};

/// @brief The step-edit region [@p pos, @p pos + @p length) held on @p clip, for as long as it lives.
struct HeldRegion {
	HeldRegion(Clip& clip, uint32_t pos, uint32_t length) {
		view.setActiveModControllableTimelineCounter(&clip, false);
		view.setModRegion(pos, length, 0);
	}
	~HeldRegion() {
		view.setModRegion();
		view.releaseActiveModControllable();
	}
	HeldRegion(const HeldRegion&) = delete;
	HeldRegion& operator=(const HeldRegion&) = delete;
};

/// @brief A synth with a session clip of its own, in @p song.
spec::SongFixture addSynth(Song* song) {
	spec::SongFixture f{.song = song, .clip = spec::detail::make_session_clip(song)};
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->setupAsDefaultSynth(&params);
	spec::detail::attach(song, f.clip, synth, params);
	f.instrument = synth;
	return f;
}

/// @brief A kit with no rows and a session clip of its own, in @p song.
spec::SongFixture addKit(Song* song) {
	spec::SongFixture f{.song = song, .clip = spec::detail::make_session_clip(song)};
	ParamManagerForTimeline params;
	params.setupUnpatched();
	GlobalEffectableForClip::initParams(&params);
	auto* kit = static_cast<Kit*>(StorageManager::createNewInstrument(OutputType::KIT, &params));
	kit->dirPath = "KITS";
	kit->name = "0";
	spec::detail::attach(song, f.clip, kit, params);
	f.instrument = kit;
	return f;
}

/// @brief A sound drum added to @p f's kit, its params backed up on the kit's clip, and no row yet.
SoundDrum* addSoundDrum(spec::SongFixture& f) {
	auto* drum = spec::detail::place<SoundDrum>();
	ParamManagerForTimeline params;
	params.setupWithPatching();
	Sound::initParams(&params);
	drum->setupAsSample(&params);
	f.song->live.param_backup.backUp(drum, f.clip, &params, true);
	static_cast<Kit*>(f.instrument)->addDrum(drum);
	return drum;
}

/// @brief A drum of type @p T added to @p f's kit, with no row yet.
template <typename T>
T* addPlainDrum(spec::SongFixture& f) {
	auto* drum = spec::detail::place<T>();
	static_cast<Kit*>(f.instrument)->addDrum(drum);
	return drum;
}

/// @brief A row for @p drum at the end of @p f's kit clip.
void addRow(spec::SongFixture& f, Drum* drum) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
	int32_t index = 0;
	NoteRow* row = f.clip->createNewNoteRowForKit(stack, /*atStart=*/false, &index);
	row->setDrum(drum, static_cast<Kit*>(f.instrument), stack->addNoteRow(index, row));
}

/// @brief The undo log's shape: the steps it holds, and the param changes in them, "steps/changes".
std::string undoShape() {
	int steps = 0;
	int changes = 0;
	for (const Action* a = actionLogger.firstAction[BEFORE]; a != nullptr; a = a->nextAction) {
		steps++;
		for (const Consequence* c = a->firstConsequence; c != nullptr; c = c->next) {
			changes += c->type == Consequence::PARAM_CHANGE ? 1 : 0;
		}
	}
	return std::to_string(steps) + "/" + std::to_string(changes);
}

/// A song with a synth, a kit (a sound row, a MIDI row, another sound row, and a sound drum with no row), an audio
/// track, a synth whose clip is not active, and a synth put to sleep.
struct World {
	Song* song = nullptr;
	spec::SongFixture synth, kit, idle;
	SoundInstrument* asleep = nullptr;
	AudioClip* audioClip = nullptr;
	AudioOutput* audio = nullptr;
	SoundDrum *rowA = nullptr, *rowB = nullptr;
};
World makeWorld() {
	World w;
	w.song = spec::detail::make_bare_song();
	ParamManagerForTimeline loose;
	w.asleep = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &loose));
	w.asleep->setupAsDefaultSynth(&loose);
	w.song->live.param_backup.backUp(w.asleep, nullptr, &loose, true);
	w.song->addOutput(w.asleep);
	w.idle = addSynth(w.song);
	w.audioClip = spec::detail::place<AudioClip>(*w.song);
	w.audioClip->setLoopLength(kDefaultClipLength << w.song->project.transport.inside_world_tick_magnitude);
	w.audioClip->paramManager.setupUnpatched();
	GlobalEffectableForClip::initParams(&w.audioClip->paramManager);
	(void)w.song->live.clips.session_clips.insertClipAt(w.audioClip, 0);
	w.audio = w.song->createNewAudioOutput();
	w.audioClip->output = w.audio;
	w.kit = addKit(w.song);
	w.rowA = addSoundDrum(w.kit);
	auto* midiRow = addPlainDrum<MIDIDrum>(w.kit);
	w.rowB = addSoundDrum(w.kit);
	(void)addSoundDrum(w.kit);
	addRow(w.kit, w.rowB);
	addRow(w.kit, midiRow);
	addRow(w.kit, w.rowA);
	w.synth = addSynth(w.song);
	activate(*w.song, *w.synth.instrument, *w.synth.clip);
	activate(*w.song, *w.kit.instrument, *w.kit.clip);
	activate(*w.song, *w.audio, *w.audioClip);
	w.asleep->editedByUser = true;
	w.song->deleteOrHibernateOutput(w.asleep);
	return w;
}

} // namespace

// clang-format off
describe learned_resolution("SetParamValue's Resolve::LEARNED, beside the resolutions it shares a stack with", $ {
	it("writes the take while the arrangement records, once made, and marks no instrument edited", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(*f.song, *f.instrument, *f.clip);
		const ParamAddress lpf = filterOf(f, deluge::instrument::param::kLpfFrequency, *f.clip);
		RootView root;
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		const std::optional<int32_t> before = valueOf(*f.song, lpf);
		ArrangementRecording recording{f};
		expect(write(*f.song, lpf, 1 << 26, Resolve::LEARNED)).to_be_true();
		Clip* const take = f.instrument->getActiveClip();
		expect(take != nullptr && take != f.clip && take->beingRecordedFromClip == f.clip).to_be_true();
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		if (take == nullptr || take == f.clip) {
			return;
		}
		expect(valueOf(*f.song, filterOf(f, deluge::instrument::param::kLpfFrequency, *take))
		       == std::optional<int32_t>{1 << 26}).to_be_true();
		expect(valueOf(*f.song, lpf) == before).to_be_true();
		// A second write finds the same take.
		expect(write(*f.song, lpf, 1 << 25, Resolve::LEARNED)).to_be_true();
		expect(f.instrument->getActiveClip() == take).to_be_true();
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_false();
	});

	it("makes a cable depth the sound has none of, and marks no instrument edited", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(*f.song, *f.instrument, *f.clip);
		const ParamAddress cable = cableOf(f, *f.clip);
		RootView root;
		expect(valueOf(*f.song, cable).has_value()).to_be_false();
		expect(write(*f.song, cable, 1 << 26, Resolve::LEARNED)).to_be_true();
		expect(valueOf(*f.song, cable) == std::optional<int32_t>{1 << 26}).to_be_true();
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_false();
	});

	it("leaves WRITE as it was: the take, the cable and the edited mark", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(*f.song, *f.instrument, *f.clip);
		const ParamAddress lpf = filterOf(f, deluge::instrument::param::kLpfFrequency, *f.clip);
		RootView root;
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		expect(write(*f.song, cableOf(f, *f.clip), 1 << 26, Resolve::WRITE)).to_be_true();
		expect(valueOf(*f.song, cableOf(f, *f.clip)) == std::optional<int32_t>{1 << 26}).to_be_true();
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_true();
		static_cast<Instrument*>(f.instrument)->editedByUser = false;
		ArrangementRecording recording{f};
		expect(write(*f.song, lpf, 1 << 26, Resolve::WRITE)).to_be_true();
		expect(f.instrument->getActiveClip() != f.clip).to_be_true();
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_true();
	});

	it("leaves EXACT as it was: the named clip, no take, no cable, no edited mark", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(*f.song, *f.instrument, *f.clip);
		const ParamAddress lpf = filterOf(f, deluge::instrument::param::kLpfFrequency, *f.clip);
		RootView root;
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		expect(write(*f.song, cableOf(f, *f.clip), 1 << 26, Resolve::EXACT)).to_be_false();
		expect(valueOf(*f.song, cableOf(f, *f.clip)).has_value()).to_be_false();
		ArrangementRecording recording{f};
		expect(write(*f.song, lpf, 1 << 26, Resolve::EXACT)).to_be_true();
		expect(f.instrument->getActiveClip() == f.clip).to_be_true();
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged);
		expect(valueOf(*f.song, lpf) == std::optional<int32_t>{1 << 26}).to_be_true();
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_false();
	});

	it("groups two writes of one message into one undo step, as EXACT's do", _ {
		const auto message = [&](Resolve resolve) {
			spec::SongFixture f = spec::make_synth_song();
			activate(*f.song, *f.instrument, *f.clip);
			RootView root;
			actionLogger.deleteAllLogs();
			expect(write(*f.song, filterOf(f, deluge::instrument::param::kLpfFrequency, *f.clip), 1 << 26, resolve))
			    .to_be_true();
			expect(write(*f.song, filterOf(f, deluge::instrument::param::kLpfResonance, *f.clip), 1 << 25, resolve))
			    .to_be_true();
			return undoShape();
		};
		const std::string exact = message(Resolve::EXACT);
		expect(exact).to_equal(std::string{"1/2"});
		expect(message(Resolve::LEARNED)).to_equal(exact);
	});
});

describe learned_sink("the sink's learned members, as the app applies them", $ {
	it("writes through write_param as LEARNED does, and as KNOB does for a gold knob", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		activate(*f.song, *f.instrument, *f.clip);
		const ParamAddress lpf = filterOf(f, deluge::instrument::param::kLpfFrequency, *f.clip);
		const ParamAddress cable = cableOf(f, *f.clip);
		RootView root;
		controls::Sink& sink = deluge::scenes::menu::host_controls_sink();
		expect(sink.write_param(cable, 1 << 26, 0, 0, controls::Resolution::LEARNED)).to_be_true();
		expect(valueOf(*f.song, cable) == std::optional<int32_t>{1 << 26}).to_be_true();
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_false();
		expect(sink.write_param(lpf, 1 << 26, 0, 0, controls::Resolution::KNOB)).to_be_true();
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_true();
		currentSong = saved;
	});

	it("makes the take for an output once, and finds it after; an unknown output has none", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		activate(*f.song, *f.instrument, *f.clip);
		RootView root;
		controls::Sink& sink = deluge::scenes::menu::host_controls_sink();
		const uint32_t output = *f.instrument->id();
		expect(sink.take_for(output) == std::optional<uint32_t>{f.clip->id()}).to_be_true();
		expect(sink.take_for(output + 1000).has_value()).to_be_false();
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		{
			ArrangementRecording recording{f};
			const std::optional<uint32_t> take = sink.take_for(output);
			expect(take.has_value() && *take != f.clip->id()).to_be_true();
			expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
			expect(f.instrument->getActiveClip() != nullptr && take == std::optional<uint32_t>{f.instrument->getActiveClip()->id()})
			    .to_be_true();
			expect(sink.take_for(output) == take).to_be_true();
			expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		}
		currentSong = saved;
	});

	it("clears the relative flag of the entry that plays a command onto a target, and no other", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		const OwnerParamTarget target{{Kind::OUTPUT, *f.instrument->id()},
		                              project::ParamTarget{ParamSpace::ENGINE, deluge::instrument::param::kLpfFrequency}};
		const OwnerParamTarget other{{Kind::OUTPUT, *f.instrument->id()},
		                             project::ParamTarget{ParamSpace::ENGINE, deluge::instrument::param::kLpfResonance}};
		const project::MidiCommand a{.channel_or_zone = kChannel, .note_or_cc = kCc};
		const project::MidiCommand b{.channel_or_zone = kChannel, .note_or_cc = kCc + 1};
		deluge::edits::learnMappingFor(a, target);
		deluge::edits::learnMappingFor(b, other);
		const auto relative = [&](const OwnerParamTarget& t) {
			const project::Mapping* entry = f.song->project.mappings.find(t);
			return entry != nullptr && entry->relative;
		};
		expect(relative(target) && relative(other)).to_be_true();
		deluge::scenes::menu::host_controls_sink().knob_absolute(target, a);
		expect(relative(target)).to_be_false();
		expect(relative(other)).to_be_true();
		// A command the target is not learned to leaves it alone.
		deluge::scenes::menu::host_controls_sink().knob_absolute(other, a);
		expect(relative(other)).to_be_true();
		currentSong = saved;
	});

	it("takes a learned write's follow with no editor of that param open", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		activate(*f.song, *f.instrument, *f.clip);
		RootView root;
		deluge::scenes::menu::host_controls_sink().learned_written(
		    filterOf(f, deluge::instrument::param::kLpfFrequency, *f.clip), 12);
		currentSong = saved;
	});
});

describe learned_facts("the facts a learned dispatch reads", $ {
	it("lists the outputs with an active clip in play order, each with the owner its params are addressed by", _ {
		World w = makeWorld();
		Song* const saved = currentSong;
		currentSong = w.song;
		const auto outputs = deluge::scenes::menu::host_runtime_facts().playing_outputs();
		expect(outputs.size()).to_equal(size_t{3});
		if (outputs.size() == 3) {
			using PlayingOutput = controls::PlayingOutput;
			expect(outputs[0].kind == PlayingOutput::Kind::SYNTH && outputs[1].kind == PlayingOutput::Kind::KIT
			       && outputs[2].kind == PlayingOutput::Kind::AUDIO).to_be_true();
			expect(outputs[0].owner == ControlsAddress{Kind::OUTPUT, *w.synth.instrument->id()}
			       && outputs[0].param_owner == OwnerId::instrument(*w.synth.instrument->id())
			       && outputs[0].clip_id == w.synth.clip->id()).to_be_true();
			expect(outputs[1].owner == ControlsAddress{Kind::OUTPUT, *w.kit.instrument->id()}
			       && outputs[1].param_owner == OwnerId::instrument(*w.kit.instrument->id())
			       && outputs[1].clip_id == w.kit.clip->id()).to_be_true();
			expect(outputs[2].owner == ControlsAddress{Kind::OUTPUT, *w.audio->id()}
			       && outputs[2].param_owner == OwnerId::clip(w.audioClip->id())
			       && outputs[2].clip_id == w.audioClip->id()).to_be_true();
		}
		currentSong = saved;
	});

	it("lists a kit clip's sound rows in note-row order, leaving a row of another kind and a drum with no row", _ {
		World w = makeWorld();
		Song* const saved = currentSong;
		currentSong = w.song;
		const auto rows = deluge::scenes::menu::host_runtime_facts().sound_rows(w.kit.clip->id());
		expect(rows.size()).to_equal(size_t{2});
		if (rows.size() == 2) {
			expect(rows[0].drum_id == *w.rowB->id() && rows[0].param_owner == OwnerId::drum(*w.rowB->id())).to_be_true();
			expect(rows[1].drum_id == *w.rowA->id() && rows[1].param_owner == OwnerId::drum(*w.rowA->id())).to_be_true();
		}
		expect(deluge::scenes::menu::host_runtime_facts().sound_rows(w.synth.clip->id()).empty()).to_be_true();
		expect(deluge::scenes::menu::host_runtime_facts().sound_rows(0xFFFFFF).empty()).to_be_true();
		currentSong = saved;
	});

	it("reads the takeover's mode, whether a running clock records, and the cable's relative flag", _ {
		const controls::Facts& facts = deluge::scenes::menu::host_runtime_facts();
		{
			TakeoverSetting mode{MIDITakeoverMode::SCALE};
			Transport transport{RecordingMode::NORMAL};
			const controls::TakeoverInputs inputs = facts.takeover_inputs(true);
			expect(inputs.mode == controls::TakeoverMode::SCALE).to_be_true();
			expect(inputs.cable_relative && inputs.recording).to_be_true();
			expect(!inputs.step_editing && !inputs.knob_relative).to_be_true();
		}
		{
			TakeoverSetting mode{MIDITakeoverMode::PICKUP};
			Transport transport{RecordingMode::OFF};
			const controls::TakeoverInputs inputs = facts.takeover_inputs(false);
			expect(inputs.mode == controls::TakeoverMode::PICKUP).to_be_true();
			expect(!inputs.cable_relative && !inputs.recording).to_be_true();
		}
	});

	it("reads the step region held on a clip, and none for another clip or the song", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		const controls::Facts& facts = deluge::scenes::menu::host_runtime_facts();
		expect(facts.step_region(f.clip->id()).has_value()).to_be_false();
		{
			HeldRegion region{*f.clip, 96, 48};
			expect(facts.step_region(f.clip->id()) == std::optional{std::pair{int32_t{96}, int32_t{48}}}).to_be_true();
			expect(facts.step_region(f.clip->id() + 1000).has_value()).to_be_false();
			expect(facts.step_region(std::nullopt).has_value()).to_be_false();
		}
		expect(facts.step_region(f.clip->id()).has_value()).to_be_false();
		currentSong = saved;
	});

	it("reads no value, and does not crash, for a param the owner does not hold yet", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		activate(*f.song, *f.instrument, *f.clip);
		const controls::Facts& facts = deluge::scenes::menu::host_runtime_facts();
		expect(facts.param_value(cableOf(f, *f.clip)).has_value()).to_be_false();
		expect(facts.param_value(filterOf(f, deluge::instrument::param::kLpfFrequency, *f.clip)).has_value())
		    .to_be_true();
		currentSong = saved;
	});
});
// clang-format on

DELUGE_SPEC(learned_resolution_spec, learned_resolution, learned_sink, learned_facts)
