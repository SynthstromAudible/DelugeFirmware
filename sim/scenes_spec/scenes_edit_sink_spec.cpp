// The app's write sink for the scenes' target nodes (gui/ui/shell_bind_host.cpp): a target node's write becomes its
// Edit on the sound's slot, fans out over a kit's sound drums where the row applies, and is refused for a slot that
// does not resolve. Against legacy ::Song fixtures, as the sink applies Edits to a running song.

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/knob_focus.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/fan_out_rules.h"
#include "scenes/menu/nodes/sound_master.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/menu/nodes/sound_voice.h"

#include "engines/deluge/codec.h"
#include "engines/deluge/engine.h"
#include "engines/deluge/params.h"
#include "engines/deluge/settings.h"
#include "engines/deluge/sound_state.h"
#include "engines/effects/settings.h"
#include "gui/ui/shell_bind_host.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"

#include "support/menu_engine.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <string>

using namespace deluge::scenes;
using deluge::scenes::bind::Target;
using deluge::scenes::menu::Kind;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::serialization::Value;
namespace bindings = deluge::scenes::menu::bindings;

namespace {

constexpr Node kMaxVoices = menu::setting_item(1, Kind::Int, Target{.kind = Target::Kind::SOUND, .path = "maxVoices"});
constexpr Node kOsc1Type = menu::setting_item(2, Kind::Enum, Target{.kind = Target::Kind::SOUND, .path = "osc1/type"});

void set_polyphony(::Sound& sound, const char* word) {
	deluge::edits::settingFor(sound, "polyphonic", Value::makeScalar(word));
}

/// A kit row with no note row: its sound has a slot, but legacy's menu refuses to open on it.
SoundDrum* add_drum_without_note_row(spec::SongFixture& f) {
	auto* kit = static_cast<Kit*>(f.instrument);
	auto* drum = spec::detail::place<SoundDrum>();
	ParamManagerForTimeline params;
	params.setupWithPatching();
	Sound::initParams(&params);
	drum->setupAsSample(&params);
	f.song->live.param_backup.backUp(drum, f.clip, &params, true);
	kit->addDrum(drum);
	kit->selectedDrum = drum;
	return drum;
}

/// `Output::activeClip` is protected, and `Output::setActiveClip` services the audio routine for a sound drum, which
/// faults in a spec process: seeded through a member pointer taken in a derived class, as note_mode_reads_spec does.
struct OutputPeek : Output {
	static Clip*& active_clip(Output& output) { return output.*(&OutputPeek::activeClip); }
};

/// Gives @p drum's first oscillator a sample, with no file loaded.
void make_sample(SoundDrum& drum) {
	deluge::edits::settingFor(drum, "osc1/type", Value::makeScalar("sample"));
}

/// An affect-entire context on @p f's kit.
MenuContext whole_kit(spec::SongFixture& f) {
	MenuContext ctx =
	    menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
	ctx.affect_entire_held = true;
	ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
	return ctx;
}

/// The descriptor menu value of choice @p stored of instrument row @p path.
int32_t menu_of(std::string_view path, int32_t stored) {
	return deluge::engine::to_menu(*deluge::engine::find_setting(deluge::instrument::kSettings, path), stored);
}

/// Writes @p node's target = descriptor menu value @p menu through the app's sink at @p ctx.
void sink_write(const Node& node, const MenuContext& ctx, int32_t menu) {
	menu::host_write_sink().write({.target = node.bind.target, .where = ctx.addresses, .menu_value = menu});
}

} // namespace

// clang-format off
describe scenes_edit_sink_writes("a target node's write through the app's sink", ${
	it("sends one request to the menu's sink, and none for a stored choice that no longer applies", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		deluge::edits::settingFor(*drum, "osc1/type", Value::makeScalar("dx7"));
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		bind::RecordingSink recorded;
		ctx.sink = &recorded;
		bindings::write(kMaxVoices, ctx, 4);
		expect(recorded.requests.size()).to_equal(size_t(1));
		expect(recorded.requests[0].menu_value).to_equal(4);
		const auto shown = bind::view(kOsc1Type.bind.target, *ctx.model, ctx.addresses, *ctx.facts);
		expect(shown.has_value() && shown->stale.has_value()).to_be_true();
		bindings::write(kOsc1Type, ctx, *shown->stale);
		expect(recorded.requests.size()).to_equal(size_t(1));
	});

	it("applies a synth's voice count through its Edit, record and runtime alike", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		set_polyphony(*spec::menu_engine(ctx).sound, "poly");
		bindings::write(kMaxVoices, ctx, 5);
		expect(static_cast<int>(spec::menu_engine(ctx).sound->boundState()->max_voice_count)).to_equal(5);
		expect(static_cast<int>(spec::menu_engine(ctx).sound->maxVoiceCount)).to_equal(5);
		expect(bindings::read(kMaxVoices, ctx)).to_equal(5);
	});

	it("writes a kit's polyphonic sound drums' voice count, and no mono, MIDI or gate drum, under affect-entire", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* poly = spec::add_drum<SoundDrum>(f);
		SoundDrum* mono = spec::add_drum<SoundDrum>(f);
		MIDIDrum* midi = spec::add_drum<MIDIDrum>(f);
		GateDrum* gate = spec::add_drum<GateDrum>(f);
		static_cast<Kit*>(f.instrument)->selectedDrum = poly;
		set_polyphony(*poly, "poly");
		set_polyphony(*mono, "mono");
		const int before = mono->boundState()->max_voice_count;
		const auto midi_channel = midi->channel();
		const auto gate_channel = gate->channel();
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
		expect(ctx.addresses.affect_entire).to_be_true();
		bindings::write(menu::nodes::voice_count, ctx, 3);
		expect(static_cast<int>(poly->boundState()->max_voice_count)).to_equal(3);
		expect(static_cast<int>(mono->boundState()->max_voice_count)).to_equal(before);
		expect(midi->channel() == midi_channel && gate->channel() == gate_channel).to_be_true();
	});

	it("reverses a sample drum with no file loaded under affect-entire, and passes over a saw drum quietly", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* first = spec::add_drum<SoundDrum>(f);
		SoundDrum* unloaded = spec::add_drum<SoundDrum>(f);
		SoundDrum* saw = spec::add_drum<SoundDrum>(f);
		make_sample(*first);
		make_sample(*unloaded);
		deluge::edits::settingFor(*saw, "osc1/type", Value::makeScalar("saw"));
		static_cast<Kit*>(f.instrument)->selectedDrum = first;
		const MenuContext ctx = whole_kit(f);
		const uint32_t refused = deluge::gui::shellEditRefusals();
		sink_write(menu::nodes::sample0_reverse, ctx, 1);
		expect(first->boundState()->sources[0].reversed).to_be_true();
		expect(unloaded->boundState()->sources[0].reversed).to_be_true();
		expect(deluge::gui::shellEditRefusals()).to_equal(refused);
	});

	it("leaves a drum with a sample oscillator in its synth mode under affect-entire, whatever the mode written", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* saw = spec::add_drum<SoundDrum>(f);
		SoundDrum* sampled = spec::add_drum<SoundDrum>(f);
		deluge::edits::settingFor(*saw, "osc1/type", Value::makeScalar("saw"));
		deluge::edits::settingFor(*saw, "osc2/type", Value::makeScalar("saw"));
		make_sample(*sampled);
		for (SoundDrum* drum : {saw, sampled}) {
			deluge::edits::settingFor(*drum, "mode", Value::makeScalar("fm"));
		}
		static_cast<Kit*>(f.instrument)->selectedDrum = saw;
		const MenuContext ctx = whole_kit(f);
		sink_write(menu::nodes::synth_mode, ctx,
		           menu_of("mode", std::to_underlying(deluge::instrument::SynthMode::SUBTRACTIVE)));
		expect(static_cast<int>(sampled->boundState()->synth_mode)).to_equal(static_cast<int>(deluge::instrument::SynthMode::FM));
		expect(static_cast<int>(saw->boundState()->synth_mode))
		    .to_equal(static_cast<int>(deluge::instrument::SynthMode::SUBTRACTIVE));
	});

	it("routes the filters only on drums with both filters on, under affect-entire", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* both = spec::add_drum<SoundDrum>(f);
		SoundDrum* lpf_off = spec::add_drum<SoundDrum>(f);
		deluge::edits::effectSettingFor(*lpf_off, "lpfMode", Value::makeScalar("Off"));
		static_cast<Kit*>(f.instrument)->selectedDrum = both;
		const MenuContext ctx = whole_kit(f);
		const auto before = lpf_off->effects().filter.route;
		const auto& row = *deluge::engine::find_setting(deluge::effects::kEffectSettings, "filterRoute");
		const bind::Target route{.kind = bind::Target::Kind::EFFECT, .path = "filterRoute",
		                         .fan_out_when = menu::fan_out::kBothFilters};
		menu::host_write_sink().write({.target = route, .where = ctx.addresses,
		                               .menu_value = deluge::engine::to_menu(row, 2)}); // parallel
		expect(both->effects().filter.route == deluge::effects::FilterRoute::PARALLEL).to_be_true();
		expect(lpf_off->effects().filter.route == before).to_be_true();
	});

	it("ends the kit arpeggiator's note when a loop mode becomes once, under affect-entire or not", _ {
		for (const bool entire : {true, false}) {
			spec::SongFixture f = spec::make_kit_song();
			SoundDrum* sampled = spec::add_drum<SoundDrum>(f);
			make_sample(*sampled);
			// The note-off reaches the kit's arpeggiator from a drum that allows note tails, as a saw does.
			SoundDrum* drum = spec::add_drum<SoundDrum>(f);
			deluge::edits::settingFor(*drum, "osc1/type", Value::makeScalar("saw"));
			auto* kit = static_cast<Kit*>(f.instrument);
			kit->selectedDrum = drum;
			OutputPeek::active_clip(*kit) = f.clip; // the kit plays the clip, so its arpeggiator holds the note
			int32_t index = 0;
			(void)f.clip->getNoteRowForDrum(drum, &index);
			ArpReturnInstruction instruction;
			const int16_t mpe[kNumExpressionDimensions]{};
			kit->arpeggiator.noteOn(&f.clip->arpSettings, index, 64, &instruction, MIDI_CHANNEL_NONE, mpe);
			expect(kit->arpeggiator.hasAnyInputNotesActive()).to_be_true();
			const MenuContext ctx =
			    entire ? whole_kit(f) : menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
			sink_write(menu::nodes::sample0_repeat, ctx,
			           menu_of("osc1/loopMode", std::to_underlying(deluge::instrument::SampleRepeatMode::ONCE)));
			expect(kit->arpeggiator.hasAnyInputNotesActive()).to_be_false();
			OutputPeek::active_clip(*kit) = nullptr;
		}
	});

	it("leaves the kit arpeggiator's note when a kit-wide loop mode write reaches no sound drum", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* midi = spec::add_drum<MIDIDrum>(f);
		auto* kit = static_cast<Kit*>(f.instrument);
		kit->selectedDrum = midi;
		OutputPeek::active_clip(*kit) = f.clip;
		int32_t index = 0;
		(void)f.clip->getNoteRowForDrum(midi, &index);
		ArpReturnInstruction instruction;
		const int16_t mpe[kNumExpressionDimensions]{};
		kit->arpeggiator.noteOn(&f.clip->arpSettings, index, 64, &instruction, MIDI_CHANNEL_NONE, mpe);
		// Legacy sends the note-off from inside its loop over the kit's sound drums (`sample/repeat.h:58-64`).
		menu::host_write_sink().write(
		    {.target = menu::nodes::sample0_repeat.bind.target,
		     .where = bind::Addresses{.kit_output = kit->id(), .affect_entire = true},
		     .menu_value = menu_of("osc1/loopMode", std::to_underlying(deluge::instrument::SampleRepeatMode::ONCE))});
		expect(kit->arpeggiator.hasAnyInputNotesActive()).to_be_true();
		OutputPeek::active_clip(*kit) = nullptr;
	});

	it("edits the whole kit's chain alone with affect-entire held, as legacy does", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		auto* kit = static_cast<Kit*>(f.instrument);
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/true);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
		expect(ctx.addresses.affect_entire).to_be_false();
		const auto row_before = drum->effects().filter.lpf_mode;
		const auto& row = *deluge::engine::find_setting(deluge::effects::kEffectSettings, "lpfMode");
		menu::host_write_sink().write({.target = {.kind = bind::Target::Kind::EFFECT, .path = "lpfMode"},
		                               .where = ctx.addresses,
		                               .menu_value = deluge::engine::to_menu(row, 4)}); // SVF_Notch
		const auto chain = deluge::project::ChainAddress{.id = kit->id().value_or(0)};
		expect(deluge::bridge::chainOwnerAt(chain, *f.song)->effects().filter.lpf_mode
		       == deluge::effects::FilterMode::SVF_NOTCH).to_be_true();
		expect(drum->effects().filter.lpf_mode == row_before).to_be_true();
	});

	it("writes only the selected drum, under affect-entire, for a setting legacy never writes kit-wide", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* other = spec::add_drum<SoundDrum>(f);
		SoundDrum* selected = spec::add_drum<SoundDrum>(f);
		static_cast<Kit*>(f.instrument)->selectedDrum = selected;
		const auto before = other->boundState()->sources[0].osc_type;
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
		bindings::write(menu::nodes::osc0_type, ctx, 4); // the fifth listed type: saw
		expect(selected->boundState()->sources[0].osc_type == deluge::instrument::OscType::SAW).to_be_true();
		expect(other->boundState()->sources[0].osc_type == before).to_be_true();
	});

	it("each knob request reaches its Edit or command, and the recording sink keeps it", _ {
		spec::SongFixture f = spec::make_synth_song();
		bind::WriteSink& sink = menu::host_write_sink();
		const uint32_t id = *f.instrument->id();
		deluge::project::Song& model = f.song->project;

		expect(sink.knob_mode(deluge::project::TrackAddress{id}, 3)).to_be_true();
		expect(model.output_structure.find(id)->mod_knob_mode).to_equal(uint8_t{3});
		expect(sink.knob_mode(deluge::project::SongAddress{}, 2)).to_be_true();
		expect(model.mod_knob_mode).to_equal(uint8_t{2});

		const deluge::project::ChainAddress song_chain{.kind = deluge::project::ChainAddress::Kind::SONG};
		expect(sink.chain_view(song_chain, "currentFilterType", "hpf")).to_be_true();
		expect(model.master_effects.filter_type).to_equal(uint8_t{1});
		expect(sink.chain_view(song_chain, "currentFilterType", "none")).to_be_false();

		std::vector<deluge::project::ControllerMapping> controls = model.output_structure.find(id)->controls;
		const deluge::project::ModKnobControl upper{.button = 1, .knob = 1};
		for (deluge::project::ControllerMapping& mapping : controls) {
			if (mapping.control == upper) {
				mapping.target = deluge::instrument::param::kHpfFrequency;
			}
		}
		const deluge::project::ControlsAddress synth{.kind = deluge::project::ControlsAddress::Kind::OUTPUT, .id = id};
		expect(sink.write_controls(synth, controls)).to_be_true();
		expect(model.output_structure.find(id)->controls == controls).to_be_true();

		const bind::KnobFocus song_focus{.owner = bind::SongFocus{}};
		expect(sink.stutter(song_focus, true)).to_be_true();
		expect(f.song->live.stutter.active()).to_be_true();
		expect(sink.stutter(song_focus, false)).to_be_true();
		expect(f.song->live.stutter.active()).to_be_false();

		const bind::KnobFocus sound_focus{
		    .owner = bind::SoundFocus{.slot = {.kind = deluge::project::SlotAddress::Kind::OUTPUT, .id = id},
		                              .clip = deluge::project::ClipAddress{f.clip->id()}}};
		expect(sink.grab_region(sound_focus, 0)).to_be_true();
		expect(sink.grab_region(bind::KnobFocus{}, 0)).to_be_false();

		bind::RecordingSink recording;
		(void)recording.knob_mode(deluge::project::TrackAddress{id}, 3);
		(void)recording.chain_view(song_chain, "currentFilterType", "hpf");
		(void)recording.write_controls(synth, controls);
		(void)recording.stutter(song_focus, true);
		(void)recording.grab_region(sound_focus, 96);
		using GK = bind::GestureRequest::Kind;
		expect(recording.gestures.size()).to_equal(size_t{5});
		if (recording.gestures.size() == 5) {
			const auto& g = recording.gestures;
			expect(g[0].kind == GK::KNOB_MODE && g[0].knob_owner == std::optional<deluge::project::KnobModeOwner>{
			                                                             deluge::project::TrackAddress{id}}
			       && g[0].value == 3)
			    .to_be_true();
			expect(g[1].kind == GK::CHAIN_VIEW && g[1].chain == song_chain && g[1].key == "currentFilterType"
			       && g[1].text == "hpf")
			    .to_be_true();
			expect(g[2].kind == GK::CONTROLS && g[2].controls_address == synth && g[2].controls == controls)
			    .to_be_true();
			expect(g[3].kind == GK::STUTTER && g[3].focus == song_focus && g[3].begin).to_be_true();
			expect(g[4].kind == GK::GRAB_REGION && g[4].focus == sound_focus && g[4].value == 96).to_be_true();
		}
	});

	it("binds nothing through the app for a song it does not play", _ {
		spec::SongFixture other = spec::make_synth_song();
		spec::SongFixture playing = spec::make_synth_song(); // the fixture makes the last song built the current one
		const MenuContext ctx = menu::context_for(*other.song, deluge::gui::menuViewOf(*other.song, other.clip), 0, false, false, false, false);
		expect(ctx.facts == nullptr && ctx.sink == nullptr).to_be_true();
		expect(bindings::relevant(kMaxVoices, ctx)).to_be_false();
		(void)playing;
	});

	it("leaves each sound playing its slot's state after its menus write it", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		bindings::write(kMaxVoices, ctx, 7);
		bindings::write(menu::nodes::osc0_type, ctx, 4);
		deluge::project::InstrumentSlot* slot = f.song->project.resolveSlot(*ctx.addresses.slot);
		expect(slot != nullptr).to_be_true();
		expect(slot->state.as<deluge::instrument::DelugeInstrument>() == drum->boundState()).to_be_true();
		const auto voices = deluge::instrument::setting_value(*slot->state.as<deluge::instrument::DelugeInstrument>(), "maxVoices");
		expect(voices.has_value() && voices->str() == "7").to_be_true();
	});

	it("refuses a request whose slot does not resolve, and changes nothing", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		const int before = spec::menu_engine(ctx).sound->boundState()->max_voice_count;
		const uint32_t refused = deluge::gui::shellEditRefusals();
		menu::host_write_sink().write(bind::WriteRequest{
		    .target = kMaxVoices.bind.target,
		    .where = bind::Addresses{.slot = deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::DRUM, 9999}},
		    .menu_value = 2});
		expect(deluge::gui::shellEditRefusals()).to_equal(refused + 1);
		expect(static_cast<int>(spec::menu_engine(ctx).sound->boundState()->max_voice_count)).to_equal(before);
	});

	it("shows no target node on a kit row with no note row", _ {
		spec::SongFixture f = spec::make_kit_song();
		add_drum_without_note_row(f);
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		expect(ctx.resolved).to_be_false();
		expect(bind::view(kMaxVoices.bind.target, *ctx.model, ctx.addresses, *ctx.facts).has_value()).to_be_false();
		expect(bindings::relevant(kMaxVoices, ctx)).to_be_false();
	});
});

DELUGE_SPEC(scenes_edit_sink_spec, scenes_edit_sink_writes)
