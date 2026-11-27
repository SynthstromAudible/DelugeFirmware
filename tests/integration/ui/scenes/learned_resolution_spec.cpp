/// @file
/// @brief What a learned dispatch asks of the app, one member at a time: the write resolution `Resolve::LEARNED` (the
///        arrangement's take, a cable made, no owner marked edited, the undo step the legacy writes make), the sink's
///        learned members, and the facts a dispatch reads (the playing outputs, a kit's sound rows, the takeover's
///        inputs, the held step region, a param that does not exist yet).

#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_engine.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence_param_change.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/edits/set_param_value.h"
#include "model/fx/chain_params.h"
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
#include "project/mapping_table.h"
#include "storage/storage_manager.h"
#include "synth/modulation/cable.h"

#include <optional>
#include <string>
#include <vector>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/learned_fixture.h"
#include "support/printers/controls_facts_printer.h"
#include "support/printers/param_id_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
namespace project = deluge::project;
namespace controls = deluge::ui::controls;

using project::ControlsAddress;
using project::OwnerId;
using project::OwnerParamTarget;
using project::ParamAddress;
using project::ParamSpace;
using Resolve = deluge::edits::SetParamValue::Resolve;
using Kind = ControlsAddress::Kind;
using namespace deluge::ui::scenes::spec::learned;

extern uint32_t whichMainRowsNeedRendering; // ui.cpp: the main pad rows marked for rendering

namespace {

constexpr uint8_t kChannel = 0;
constexpr uint8_t kCc = 1;

/// @brief The depth of the cable from @p source onto patched param @p p, as the table names it.
project::ParamTarget depthOf(int32_t p, PatchSource source) {
	return project::engine_target(
	    deluge::bridge::controlTargetOf(
	        deluge::modulation::knobTargetOf(deluge::synth::CableTarget::depth_of(static_cast<uint8_t>(p), source)),
	        params::Kind::UNPATCHED_SOUND)
	        .id.value);
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
	deluge::model::initGlobalParams(&w.audioClip->paramManager);
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
		const ParamAddress lpf = filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *f.clip);
		RootView root;
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		const std::optional<int32_t> before = valueOf(*f.song, lpf);
		ArrangementRecording recording{f};
		expect(write(*f.song, lpf, 1 << 26, Resolve::LEARNED)).to_be_true();
		Clip* const take = f.instrument->getActiveClip();
		expect(take).not_().to_be_null("the take");
		expect(take).not_().to_equal(f.clip, "the take");
		if (take != nullptr) {
			expect(take->beingRecordedFromClip).to_equal(f.clip, "the clip the take records from");
		}
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		if (take == nullptr || take == f.clip) {
			return;
		}
		expect(valueOf(*f.song, filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *take)))
		    .to_equal(std::optional<int32_t>{1 << 26}, "the take's LPF");
		expect(valueOf(*f.song, lpf)).to_equal(before, "the session clip's LPF");
		// A second write finds the same take.
		expect(write(*f.song, lpf, 1 << 25, Resolve::LEARNED)).to_be_true();
		expect(f.instrument->getActiveClip()).to_equal(take, "the active clip");
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
		expect(valueOf(*f.song, cable)).to_equal(std::optional<int32_t>{1 << 26}, "the cable depth");
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_false();
	});

	it("leaves WRITE as it was: the take, the cable and the edited mark", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(*f.song, *f.instrument, *f.clip);
		const ParamAddress lpf = filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *f.clip);
		RootView root;
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		expect(write(*f.song, cableOf(f, *f.clip), 1 << 26, Resolve::WRITE)).to_be_true();
		expect(valueOf(*f.song, cableOf(f, *f.clip))).to_equal(std::optional<int32_t>{1 << 26}, "the cable depth");
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_true();
		static_cast<Instrument*>(f.instrument)->editedByUser = false;
		ArrangementRecording recording{f};
		expect(write(*f.song, lpf, 1 << 26, Resolve::WRITE)).to_be_true();
		expect(f.instrument->getActiveClip()).not_().to_equal(static_cast<Clip*>(f.clip), "the active clip");
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_true();
	});

	it("leaves EXACT as it was: the named clip, no take, no cable, no edited mark", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(*f.song, *f.instrument, *f.clip);
		const ParamAddress lpf = filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *f.clip);
		RootView root;
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		expect(write(*f.song, cableOf(f, *f.clip), 1 << 26, Resolve::EXACT)).to_be_false();
		expect(valueOf(*f.song, cableOf(f, *f.clip)).has_value()).to_be_false();
		ArrangementRecording recording{f};
		expect(write(*f.song, lpf, 1 << 26, Resolve::EXACT)).to_be_true();
		expect(f.instrument->getActiveClip()).to_equal(static_cast<Clip*>(f.clip), "the active clip");
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged);
		expect(valueOf(*f.song, lpf)).to_equal(std::optional<int32_t>{1 << 26}, "the LPF");
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_false();
	});

	it("groups two writes of one message into one undo step, as EXACT's do", _ {
		const auto message = [&](Resolve resolve) {
			spec::SongFixture f = spec::make_synth_song();
			activate(*f.song, *f.instrument, *f.clip);
			RootView root;
			actionLogger.deleteAllLogs();
			expect(write(*f.song, filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *f.clip), 1 << 26, resolve))
			    .to_be_true();
			expect(write(*f.song, filterOf(f, deluge::instruments::classic::param::kLpfResonance, *f.clip), 1 << 25, resolve))
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
		deluge::ui::scenes::spec::make_current(f.song);
		activate(*f.song, *f.instrument, *f.clip);
		const ParamAddress lpf = filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *f.clip);
		const ParamAddress cable = cableOf(f, *f.clip);
		RootView root;
		controls::Sink& sink = *deluge::gui::shell_host::host_ports().controls_sink;
		expect(sink.write_param(cable, 1 << 26, 0, 0, controls::Resolution::LEARNED)).to_be_true();
		expect(valueOf(*f.song, cable)).to_equal(std::optional<int32_t>{1 << 26}, "the cable depth");
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_false();
		expect(sink.write_param(lpf, 1 << 26, 0, 0, controls::Resolution::KNOB)).to_be_true();
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_true();
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("makes the take for an output once, and finds it after; an unknown output has none", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		activate(*f.song, *f.instrument, *f.clip);
		RootView root;
		controls::Sink& sink = *deluge::gui::shell_host::host_ports().controls_sink;
		const uint32_t output = *f.instrument->id();
		expect(sink.take_for(output)).to_equal(std::optional<uint32_t>{f.clip->id()}, "the output's take");
		expect(sink.take_for(output + 1000).has_value()).to_be_false();
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();
		{
			ArrangementRecording recording{f};
			const std::optional<uint32_t> take = sink.take_for(output);
			expect(take.has_value()).to_be_true("the output's take");
			if (take.has_value()) {
				expect(*take).not_().to_equal(f.clip->id(), "the output's take");
			}
			expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
			Clip* const active = f.instrument->getActiveClip();
			expect(active).not_().to_be_null("the output's active clip");
			if (active != nullptr) {
				expect(take).to_equal(std::optional<uint32_t>{active->id()}, "the output's take");
			}
			expect(sink.take_for(output)).to_equal(take, "the output's take, asked again");
			expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		}
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("makes only a cable's depth for a learned knob: a param it holds is found, an expression lane is not made", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		activate(*f.song, *f.instrument, *f.clip);
		RootView root;
		controls::Sink& sink = *deluge::gui::shell_host::host_ports().controls_sink;
		const ParamAddress cable = cableOf(f, *f.clip);
		expect(valueOf(*f.song, cable).has_value()).to_be_false();
		expect(sink.make_param(cable)).to_be_true();
		expect(valueOf(*f.song, cable)).to_equal(std::optional<int32_t>{0}, "the made cable depth");
		expect(sink.make_param(filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *f.clip))).to_be_true();
		const ParamAddress bend{.owner = OwnerId::instrument(*f.instrument->id()),
		                        .space = ParamSpace::EXPRESSION,
		                        .id = deluge::plugin::ParamId{0},
		                        .clip_id = f.clip->id()};
		expect(f.clip->paramManager.getExpressionParamSet()).to_be_null("the clip's expression params");
		expect(sink.make_param(bend)).to_be_false();
		expect(f.clip->paramManager.getExpressionParamSet()).to_be_null("the clip's expression params");
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("clears the relative flag of the entry that plays a command onto a target, and no other", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		const OwnerParamTarget target{{Kind::OUTPUT, *f.instrument->id()},
		                              project::ParamTarget{ParamSpace::ENGINE, deluge::instruments::classic::param::kLpfFrequency}};
		const OwnerParamTarget other{{Kind::OUTPUT, *f.instrument->id()},
		                             project::ParamTarget{ParamSpace::ENGINE, deluge::instruments::classic::param::kLpfResonance}};
		const project::MidiCommand a{.channel_or_zone = kChannel, .note_or_cc = kCc};
		const project::MidiCommand b{.channel_or_zone = kChannel, .note_or_cc = kCc + 1};
		deluge::edits::learnMappingFor(a, target);
		deluge::edits::learnMappingFor(b, other);
		const auto relative = [&](const OwnerParamTarget& t) {
			const project::Mapping* entry = f.song->project.mappings.find(t);
			return entry != nullptr && entry->relative;
		};
		expect(relative(target)).to_be_true("the target's relative flag");
		expect(relative(other)).to_be_true("the other's relative flag");
		deluge::gui::shell_host::host_ports().controls_sink->knob_absolute(target, a);
		expect(relative(target)).to_be_false();
		expect(relative(other)).to_be_true();
		// A command the target is not learned to leaves it alone.
		deluge::gui::shell_host::host_ports().controls_sink->knob_absolute(other, a);
		expect(relative(other)).to_be_true();
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("follows a learned write in the views as legacy's handlers do: a song param in either, a clip's in the editor only", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::SongFixture other = addSynth(f.song);
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		activate(*f.song, *f.instrument, *f.clip);
		f.song->setCurrentClip(f.clip);
		controls::Sink& sink = *deluge::gui::shell_host::host_ports().controls_sink;
		const ParamAddress lpf = filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *f.clip);
		const project::ParamTarget volume = project::engine_target(
		    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(params::UNPATCHED_START + params::UNPATCHED_VOLUME),
		                                    params::Kind::UNPATCHED_GLOBAL).id.value);
		const ParamAddress songVolume{.owner = OwnerId::song(), .space = volume.space, .id = volume.id};
		const auto lane = [&](const ParamAddress& address) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(*f.song, address, memory);
			return stack != nullptr ? std::pair{stack->paramId, stack->paramCollection->getParamKind()}
			                        : std::pair{-1, params::Kind::NONE};
		};
		const auto [lpfId, lpfKind] = lane(lpf);
		const auto [volId, volKind] = lane(songVolume);
		f.clip->lastSelectedParamID = lpfId;
		f.clip->lastSelectedParamKind = lpfKind;
		other.clip->lastSelectedParamID = lpfId;
		other.clip->lastSelectedParamKind = lpfKind;
		f.song->lastSelectedParamID = volId;
		f.song->lastSelectedParamKind = volKind;
		// What a follow does: the editor's grid marked for rendering, the performance display drawn.
		struct Seen {
			bool grid;
			bool display;
		};
		const auto follow = [&](UI& root, const ParamAddress& address) {
			setRootUILowLevel(&root);
			whichMainRowsNeedRendering = 0;
			const uint32_t drawn = deluge::hid::display::OLED::getMainImageGeneration();
			sink.learned_written(address, 5);
			const Seen seen{whichMainRowsNeedRendering != 0, deluge::hid::display::OLED::getMainImageGeneration() != drawn};
			nullifyUIs();
			return seen;
		};
		const bool savedArranger = automationView.onArrangerView;
		const bool savedEditing = performanceView.defaultEditingMode;
		const auto savedPress = performanceView.lastPadPress;
		const bool savedParamEditing = performanceView.editingParam;
		automationView.onArrangerView = false;
		performanceView.defaultEditingMode = false;
		performanceView.editingParam = false;
		performanceView.lastPadPress.isActive = true;
		performanceView.lastPadPress.param = volume.id;
		// A clip's param: the editor's grid on the clip it shows, and nothing else.
		expect(follow(automationView, lpf).grid).to_be_true();
		expect(follow(automationView, lpf).display).to_be_false();
		expect(follow(performanceView, lpf).grid).to_be_false();
		expect(follow(performanceView, lpf).display).to_be_false();
		f.song->setCurrentClip(other.clip);
		expect(follow(automationView, lpf).grid).to_be_false(); // the editor shows another clip
		f.song->setCurrentClip(f.clip);
		automationView.onArrangerView = true;
		expect(follow(automationView, lpf).grid).to_be_false(); // the arranger shows no clip's param
		// The song's param: the arranger's grid, or off the arranger nothing in the editor; the performance display.
		expect(follow(automationView, songVolume).grid).to_be_true();
		automationView.onArrangerView = false;
		expect(follow(automationView, songVolume).grid).to_be_false();
		expect(follow(performanceView, songVolume).display).to_be_true();
		expect(follow(performanceView, songVolume).grid).to_be_false();
		// A session root follows nothing.
		const Seen session = follow(sessionView, songVolume);
		expect(session.grid).to_be_false("the session's grid");
		expect(session.display).to_be_false("the session's display");
		automationView.onArrangerView = savedArranger;
		performanceView.defaultEditingMode = savedEditing;
		performanceView.editingParam = savedParamEditing;
		performanceView.lastPadPress = savedPress;
		deluge::ui::scenes::spec::make_current(saved);
	});
});

describe learned_facts("the facts a learned dispatch reads", $ {
	it("lists the outputs with an active clip in play order, each with the owner its params are addressed by", _ {
		World w = makeWorld();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(w.song);
		const auto outputs = deluge::gui::shell_host::host_ports().controls->playing_outputs();
		expect(outputs.size()).to_equal(size_t{3});
		if (outputs.size() == 3) {
			using PlayingOutput = controls::PlayingOutput;
			expect(outputs[0]).to_equal(PlayingOutput{.owner = ControlsAddress{Kind::OUTPUT, *w.synth.instrument->id()},
			                                          .param_owner = OwnerId::instrument(*w.synth.instrument->id()),
			                                          .kind = PlayingOutput::Kind::SYNTH,
			                                          .clip_id = w.synth.clip->id()},
			                            "the first output");
			expect(outputs[1]).to_equal(PlayingOutput{.owner = ControlsAddress{Kind::OUTPUT, *w.kit.instrument->id()},
			                                          .param_owner = OwnerId::instrument(*w.kit.instrument->id()),
			                                          .kind = PlayingOutput::Kind::KIT,
			                                          .clip_id = w.kit.clip->id()},
			                            "the second output");
			expect(outputs[2]).to_equal(PlayingOutput{.owner = ControlsAddress{Kind::OUTPUT, *w.audio->id()},
			                                          .param_owner = OwnerId::clip(w.audioClip->id()),
			                                          .kind = PlayingOutput::Kind::AUDIO,
			                                          .clip_id = w.audioClip->id()},
			                            "the third output");
		}
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("lists a kit clip's sound rows in note-row order, leaving a row of another kind and a drum with no row", _ {
		World w = makeWorld();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(w.song);
		const auto rows = deluge::gui::shell_host::host_ports().controls->sound_rows(w.kit.clip->id());
		expect(rows.size()).to_equal(size_t{2});
		if (rows.size() == 2) {
			expect(rows[0]).to_equal(controls::KitRow{.drum_id = *w.rowB->id(), .param_owner = OwnerId::drum(*w.rowB->id())},
			                         "the first row");
			expect(rows[1]).to_equal(controls::KitRow{.drum_id = *w.rowA->id(), .param_owner = OwnerId::drum(*w.rowA->id())},
			                         "the second row");
		}
		expect(deluge::gui::shell_host::host_ports().controls->sound_rows(w.synth.clip->id()).empty()).to_be_true();
		expect(deluge::gui::shell_host::host_ports().controls->sound_rows(0xFFFFFF).empty()).to_be_true();
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("keeps each member's answer across the other's calls and a write that clones a take", _ {
		World w = makeWorld();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(w.song);
		RootView root;
		const controls::Facts& facts = *deluge::gui::shell_host::host_ports().controls;
		const auto outputs = facts.playing_outputs();
		const std::vector<controls::PlayingOutput> before(outputs.begin(), outputs.end());
		expect(before.size()).to_equal(size_t{3});
		// A dispatcher's walk: each output's clip asked for its rows while the outputs are still read.
		size_t rowCount = 0;
		std::vector<controls::KitRow> kitRows;
		for (const controls::PlayingOutput& output : outputs) {
			const auto rows = facts.sound_rows(output.clip_id);
			rowCount += rows.size();
			if (output.kind == controls::PlayingOutput::Kind::KIT) {
				kitRows.assign(rows.begin(), rows.end());
			}
		}
		expect(rowCount).to_equal(size_t{2});
		expect(std::vector<controls::PlayingOutput>(outputs.begin(), outputs.end())).to_equal(before, "the outputs");
		// A write that clones the synth's clip into a take leaves both spans as they were.
		const auto rows = facts.sound_rows(w.kit.clip->id());
		{
			ArrangementRecording recording{w.synth};
			expect(write(*w.song, filterOf(w.synth, deluge::instruments::classic::param::kLpfFrequency, *w.synth.clip), 1 << 26,
			             Resolve::LEARNED)).to_be_true();
			expect(w.synth.instrument->getActiveClip())
			    .not_()
			    .to_equal(static_cast<Clip*>(w.synth.clip), "the synth's active clip");
			expect(std::vector<controls::PlayingOutput>(outputs.begin(), outputs.end()))
			    .to_equal(before, "the outputs after the clone");
			expect(std::vector<controls::KitRow>(rows.begin(), rows.end())).to_equal(kitRows, "the kit's rows after the clone");
			// The next call answers the take.
			const auto after = facts.playing_outputs();
			expect(after.size()).to_equal(size_t{3}, "the outputs asked again");
			if (after.size() == 3) {
				expect(after[0].clip_id).to_equal(w.synth.instrument->getActiveClip()->id(), "the synth's clip asked again");
			}
		}
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("reads the takeover's mode, whether a running clock records, and the cable's relative flag", _ {
		const controls::Facts& facts = *deluge::gui::shell_host::host_ports().controls;
		{
			TakeoverSetting mode{MIDITakeoverMode::SCALE};
			Transport transport{RecordingMode::NORMAL};
			const controls::TakeoverInputs inputs = facts.takeover_inputs(true);
			expect(int(inputs.mode)).to_equal(int(controls::TakeoverMode::SCALE), "the takeover mode");
			expect(inputs.cable_relative).to_be_true("the cable's relative flag");
			expect(inputs.recording).to_be_true("the recording flag");
			expect(inputs.step_editing).to_be_false("the step editing flag");
			expect(inputs.knob_relative).to_be_false("the knob's relative flag");
		}
		{
			TakeoverSetting mode{MIDITakeoverMode::PICKUP};
			Transport transport{RecordingMode::OFF};
			const controls::TakeoverInputs inputs = facts.takeover_inputs(false);
			expect(int(inputs.mode)).to_equal(int(controls::TakeoverMode::PICKUP), "the takeover mode");
			expect(inputs.cable_relative).to_be_false("the cable's relative flag");
			expect(inputs.recording).to_be_false("the recording flag");
		}
	});

	it("reads the step region held on a clip, and none for another clip or the song", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		const controls::Facts& facts = *deluge::gui::shell_host::host_ports().controls;
		expect(facts.step_region(f.clip->id()).has_value()).to_be_false();
		{
			HeldRegion region{*f.clip, 96, 48};
			expect(facts.step_region(f.clip->id()))
			    .to_equal(std::optional{std::pair{int32_t{96}, int32_t{48}}}, "the held step region");
			expect(facts.step_region(f.clip->id() + 1000).has_value()).to_be_false();
			expect(facts.step_region(std::nullopt).has_value()).to_be_false();
		}
		expect(facts.step_region(f.clip->id()).has_value()).to_be_false();
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("names a sound's params by the instrument's rows and a chain's by the global effectable's", _ {
		const controls::Facts& facts = *deluge::gui::shell_host::host_ports().controls;
		const project::ParamTarget lpf{.space = ParamSpace::ENGINE, .id = deluge::instruments::classic::param::kLpfFrequency};
		const deluge::plugin::ParamInfo* sound = facts.param_info(deluge::plugin::kDelugeInstrument, lpf);
		expect(sound).not_().to_be_null("the sound's LPF info");
		if (sound != nullptr) {
			expect(sound->id).to_equal(lpf.id, "the sound's LPF info");
		}
		const deluge::plugin::ParamInfo* chain = facts.param_info(
		    deluge::plugin::kGlobalEffectable, {.space = ParamSpace::ENGINE, .id = deluge::host::chains::kParams[0].id});
		expect(chain).to_equal(&deluge::host::chains::kParams[0], "the chain's first param info");
		expect(facts.param_info(deluge::plugin::kDelugeInstrument, depthOf(params::LOCAL_LPF_FREQ, PatchSource::LFO_GLOBAL_1)))
		    .to_be_null("a cable depth's info");
		expect(facts.param_info(deluge::plugin::kDelugeEffects, lpf)).to_be_null("the effects' LPF info");
		expect(facts.param_info(deluge::plugin::kDelugeInstrument, {.space = ParamSpace::EXPRESSION, .id = lpf.id}))
		    .to_be_null("an expression lane's info");
	});

	it("reads no value, and does not crash, for a param the owner does not hold yet", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		activate(*f.song, *f.instrument, *f.clip);
		const controls::Facts& facts = *deluge::gui::shell_host::host_ports().controls;
		expect(facts.param_value(cableOf(f, *f.clip)).has_value()).to_be_false();
		expect(facts.param_value(filterOf(f, deluge::instruments::classic::param::kLpfFrequency, *f.clip)).has_value())
		    .to_be_true();
		deluge::ui::scenes::spec::make_current(saved);
	});
});
// clang-format on

DELUGE_SPEC(learned_resolution_spec, learned_resolution, learned_sink, learned_facts)
