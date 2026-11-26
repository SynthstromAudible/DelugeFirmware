// The app's write sink for the scenes' target nodes (gui/ui/shell_host/edit_sink.cpp): a target node's write becomes
// its Edit on the sound's slot, fans out over a kit's sound drums where the row applies, and is refused for a slot that
// does not resolve. Against legacy ::Song fixtures, as the sink applies Edits to a running song.

#include "fakes.h"
#include "gui/ui/menu_view_of.h"
#include "ui/scenes/bind/dispatch.h"
#include "ui/scenes/bind/knob_focus.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/menu/bind_host.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/fan_out_rules.h"
#include "ui/scenes/menu/nodes/sound_master.h"
#include "ui/scenes/menu/nodes/sound_osc.h"
#include "ui/scenes/menu/nodes/sound_voice.h"

#include "effects/settings.h"
#include "gui/ui/shell_host/shell_host.h"
#include "instruments/classic/codec.h"
#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/params.h"
#include "instruments/classic/settings.h"
#include "instruments/classic/sound_state.h"
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
#include "support/printers/controller_mapping_printer.h"
#include "support/printers/knob_focus_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/record_state.h"

#include "support/menu_context.h"
#include "support/spec_main.h"

#include <string>

using namespace deluge::ui::scenes;
using deluge::serialization::Value;
using deluge::ui::scenes::bind::Target;
using deluge::ui::scenes::menu::Kind;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
namespace bindings = deluge::ui::scenes::menu::bindings;

namespace {

constexpr Node kMaxVoices = menu::setting_item(1, Kind::Int, Target{.kind = Target::Kind::SOUND, .path = "maxVoices"});
constexpr Node kOsc2Type = menu::setting_item(2, Kind::Enum, Target{.kind = Target::Kind::SOUND, .path = "osc2/type"});

void set_polyphony(::Sound& sound, const char* word) {
	deluge::edits::settingFor(sound, "polyphonic", Value::makeScalar(word));
}

/// A kit row with no note row: its sound has a slot, but main's menu refuses to open on it.
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
	MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
	ctx.affect_entire_held = true;
	ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
	return ctx;
}

/// The descriptor menu value of choice @p stored of instrument row @p path.
int32_t menu_of(std::string_view path, int32_t stored) {
	return deluge::plugin::to_menu(*deluge::plugin::find_setting(deluge::instruments::classic::kSettings, path),
	                               stored);
}

/// Writes @p node's target = descriptor menu value @p menu through the app's sink at @p ctx.
void sink_write(const Node& node, const MenuContext& ctx, int32_t menu) {
	menu::host_ports().writes->write({.target = node.bind.target, .where = ctx.addresses, .menu_value = menu});
}

} // namespace

// clang-format off
describe scenes_edit_sink_writes("a target node's write through the app's sink", ${
	it("sends one request to the menu's sink, and none for a stored choice that no longer applies", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		deluge::edits::settingFor(*drum, "osc2/type", Value::makeScalar("dx7")); // only the first oscillator offers DX7
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		bind::test::RecordingWrites recorded;
		ctx.ports.writes = &recorded;
		bindings::write(kMaxVoices, ctx, 4);
		expect(recorded.requests.size()).to_equal(size_t(1));
		expect(recorded.requests[0].menu_value).to_equal(4);
		const auto shown = bind::view(kOsc2Type.bind.target, ctx.model, ctx.addresses, ctx.ports);
		expect(shown.has_value()).to_be_true();
		if (shown.has_value()) {
			expect(shown->stale.has_value()).to_be_true();
		}
		bindings::write(kOsc2Type, ctx, *shown->stale);
		expect(recorded.requests.size()).to_equal(size_t(1));
	});

	it("applies a synth's voice count through its Edit, record and runtime alike", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		set_polyphony(*spec::menu_engine(ctx, *f.song).sound, "poly");
		bindings::write(kMaxVoices, ctx, 5);
		expect(static_cast<int>(deluge::instruments::classic::voicing(spec::menu_engine(ctx, *f.song).sound->state()).max_voices)).to_equal(5);
		expect(static_cast<int>(spec::menu_engine(ctx, *f.song).sound->config().voicing.max_voices)).to_equal(5);
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
		const int before = deluge::instruments::classic::voicing(mono->state()).max_voices;
		const auto midi_channel = midi->channel();
		const auto gate_channel = gate->channel();
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
		expect(ctx.addresses.affect_entire).to_be_true();
		bindings::write(menu::nodes::voice_count, ctx, 3);
		expect(static_cast<int>(deluge::instruments::classic::voicing(poly->state()).max_voices)).to_equal(3);
		expect(static_cast<int>(deluge::instruments::classic::voicing(mono->state()).max_voices)).to_equal(before);
		expect(midi->channel()).to_equal(midi_channel);
		expect(gate->channel()).to_equal(gate_channel);
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
		const uint32_t refused = deluge::gui::shell_host::edit_refusals();
		sink_write(menu::nodes::sample0_reverse, ctx, 1);
		expect(deluge::instruments::classic::sample_playback(first->state(), deluge::instruments::classic::SourceId::A).reversed).to_be_true();
		expect(deluge::instruments::classic::sample_playback(unloaded->state(), deluge::instruments::classic::SourceId::A).reversed).to_be_true();
		expect(deluge::gui::shell_host::edit_refusals()).to_equal(refused);
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
		           menu_of("mode", std::to_underlying(deluge::instruments::classic::SynthMode::SUBTRACTIVE)));
		expect(static_cast<int>(deluge::instruments::classic::synth_mode(sampled->state()))).to_equal(static_cast<int>(deluge::instruments::classic::SynthMode::FM));
		expect(static_cast<int>(deluge::instruments::classic::synth_mode(saw->state())))
		    .to_equal(static_cast<int>(deluge::instruments::classic::SynthMode::SUBTRACTIVE));
	});

	it("routes the filters only on drums with both filters on, under affect-entire", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* both = spec::add_drum<SoundDrum>(f);
		SoundDrum* lpf_off = spec::add_drum<SoundDrum>(f);
		deluge::edits::effectSettingFor(lpf_off, "lpfMode", Value::makeScalar("Off"));
		static_cast<Kit*>(f.instrument)->selectedDrum = both;
		const MenuContext ctx = whole_kit(f);
		const auto before = lpf_off->effects().filter.route;
		const auto& row = *deluge::plugin::find_setting(deluge::effects::kEffectSettings, "filterRoute");
		const bind::Target route{.kind = bind::Target::Kind::EFFECT, .path = "filterRoute",
		                         .fan_out_when = menu::fan_out::kBothFilters};
		menu::host_ports().writes->write({.target = route, .where = ctx.addresses,
		                               .menu_value = deluge::plugin::to_menu(row, 2)}); // parallel
		expect(both->effects().filter.route).to_equal(deluge::effects::FilterRoute::PARALLEL);
		expect(lpf_off->effects().filter.route).to_equal(before);
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
			    entire ? whole_kit(f) : spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
			sink_write(menu::nodes::sample0_repeat, ctx,
			           menu_of("osc1/loopMode", std::to_underlying(deluge::instruments::classic::SampleRepeatMode::ONCE)));
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
		// Main sends the note-off from inside its loop over the kit's sound drums (`Repeat::writeCurrentValue` in
		// `origin/main:src/deluge/gui/menu_item/sample/repeat.h`).
		menu::host_ports().writes->write(
		    {.target = menu::nodes::sample0_repeat.bind.target,
		     .where = bind::Addresses{.kit_output = kit->id(), .affect_entire = true},
		     .menu_value = menu_of("osc1/loopMode", std::to_underlying(deluge::instruments::classic::SampleRepeatMode::ONCE))});
		expect(kit->arpeggiator.hasAnyInputNotesActive()).to_be_true();
		OutputPeek::active_clip(*kit) = nullptr;
	});

	it("edits the whole kit's chain alone with affect-entire held, as legacy does", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		auto* kit = static_cast<Kit*>(f.instrument);
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, /*kit_global_fx=*/true);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
		expect(ctx.addresses.affect_entire).to_be_false();
		const auto row_before = drum->effects().filter.lpf_mode;
		const auto& row = *deluge::plugin::find_setting(deluge::effects::kEffectSettings, "lpfMode");
		menu::host_ports().writes->write({.target = {.kind = bind::Target::Kind::EFFECT, .path = "lpfMode"},
		                               .where = ctx.addresses,
		                               .menu_value = deluge::plugin::to_menu(row, 4)}); // SVF_Notch
		const auto chain = deluge::project::ChainAddress{.id = kit->id().value_or(0)};
		expect(deluge::bridge::chainAt(chain, *f.song)->effects().filter.lpf_mode)
		    .to_equal(deluge::effects::FilterMode::SVF_NOTCH);
		expect(drum->effects().filter.lpf_mode).to_equal(row_before);
	});

	it("writes only the selected drum, under affect-entire, for a setting legacy never writes kit-wide", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* other = spec::add_drum<SoundDrum>(f);
		SoundDrum* selected = spec::add_drum<SoundDrum>(f);
		static_cast<Kit*>(f.instrument)->selectedDrum = selected;
		const auto before = deluge::instruments::classic::source_type(other->state(), deluge::instruments::classic::SourceId::A);
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
		bindings::write(menu::nodes::osc0_type, ctx, 4); // the fifth listed type: saw
		expect(deluge::instruments::classic::source_type(selected->state(), deluge::instruments::classic::SourceId::A)).to_equal(deluge::instruments::classic::OscType::SAW);
		expect(deluge::instruments::classic::source_type(other->state(), deluge::instruments::classic::SourceId::A)).to_equal(before);
	});

	it("each knob request reaches its Edit or command, and the recording sink keeps it", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::ui::controls::Sink& sink = *menu::host_ports().controls_sink;
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
				mapping.target = deluge::instruments::classic::param::kHpfFrequency;
			}
		}
		const deluge::project::ControlsAddress synth{.kind = deluge::project::ControlsAddress::Kind::OUTPUT, .id = id};
		expect(sink.write_controls(synth, controls)).to_be_true();
		expect(model.output_structure.find(id)->controls).to_equal(controls);

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

		bind::test::RecordingControlsSink recording;
		(void)recording.knob_mode(deluge::project::TrackAddress{id}, 3);
		(void)recording.chain_view(song_chain, "currentFilterType", "hpf");
		(void)recording.write_controls(synth, controls);
		(void)recording.stutter(song_focus, true);
		(void)recording.grab_region(sound_focus, 96);
		using GK = bind::test::RecordingControlsSink::Gesture::Kind;
		expect(recording.gestures.size()).to_equal(size_t{5});
		if (recording.gestures.size() == 5) {
			const auto& g = recording.gestures;
			expect(g[0].kind).to_equal(GK::KNOB_MODE);
			expect(g[0].knob_owner)
			    .to_equal(std::optional<deluge::project::KnobModeOwner>{deluge::project::TrackAddress{id}});
			expect(g[0].value).to_equal(3);
			expect(g[1].kind).to_equal(GK::CHAIN_VIEW);
			expect(g[1].chain).to_equal(song_chain);
			expect(g[1].key).to_equal("currentFilterType");
			expect(g[1].text).to_equal("hpf");
			expect(g[2].kind).to_equal(GK::CONTROLS);
			expect(g[2].controls_address).to_equal(synth);
			expect(g[2].controls).to_equal(controls);
			expect(g[3].kind).to_equal(GK::STUTTER);
			expect(g[3].focus).to_equal(song_focus);
			expect(g[3].begin).to_be_true();
			expect(g[4].kind).to_equal(GK::GRAB_REGION);
			expect(g[4].focus).to_equal(sound_focus);
			expect(g[4].value).to_equal(96);
		}
	});

	it("binds nothing through the app for a song it does not play", _ {
		spec::SongFixture other = spec::make_synth_song();
		spec::SongFixture playing = spec::make_synth_song(); // the fixture makes the last song built the current one
		const MenuContext ctx = spec::ctx_of(*other.song, deluge::gui::menuViewOf(other.clip), 0, false, false, false, false);
		expect(ctx.ports.controls).to_be_null();
		expect(ctx.ports.writes).to_be_null();
		expect(bindings::relevant(kMaxVoices, ctx)).to_be_false();
		(void)playing;
	});

	it("leaves each sound playing its slot's state after its menus write it", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		bindings::write(kMaxVoices, ctx, 7);
		bindings::write(menu::nodes::osc0_type, ctx, 4);
		deluge::project::InstrumentSlot* slot = f.song->project.resolveSlot(*ctx.addresses.slot);
		expect(slot).not_().to_be_null();
		expect(slot->state.as<deluge::instruments::classic::DelugeSynth>()).to_equal(&drum->state());
		const auto voices = deluge::instruments::classic::setting_value(*slot->state.as<deluge::instruments::classic::DelugeSynth>(), "maxVoices");
		expect(voices.has_value()).to_be_true();
		if (voices.has_value()) {
			expect(voices->str()).to_equal("7");
		}
	});

	it("refuses a request whose slot does not resolve, and changes nothing", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		const int before = deluge::instruments::classic::voicing(spec::menu_engine(ctx, *f.song).sound->state()).max_voices;
		const uint32_t refused = deluge::gui::shell_host::edit_refusals();
		menu::host_ports().writes->write(bind::WriteRequest{
		    .target = kMaxVoices.bind.target,
		    .where = bind::Addresses{.slot = deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::DRUM, 9999}},
		    .menu_value = 2});
		expect(deluge::gui::shell_host::edit_refusals()).to_equal(refused + 1);
		expect(static_cast<int>(deluge::instruments::classic::voicing(spec::menu_engine(ctx, *f.song).sound->state()).max_voices)).to_equal(before);
	});

	it("shows no target node on a kit row with no note row", _ {
		spec::SongFixture f = spec::make_kit_song();
		add_drum_without_note_row(f);
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		expect(ctx.resolved).to_be_false();
		expect(bind::view(kMaxVoices.bind.target, ctx.model, ctx.addresses, ctx.ports).has_value()).to_be_false();
		expect(bindings::relevant(kMaxVoices, ctx)).to_be_false();
	});
});

DELUGE_SPEC(scenes_edit_sink_spec, scenes_edit_sink_writes)
