/// @file
/// @brief The arpeggiator and randomizer items the kit's own shortcut table names, relevant where main's say: the rate
///        (`arpeggiator::Rate`) not on a MIDI or CV clip, a MIDI or gate drum row or a kit's own effects; the octaves
///        (`arpeggiator::Octaves`) by the latch and the selected row alone; the gate and the velocity spread not with a
///        MIDI or gate drum selected; and every column of the kit's own arp and randomizer pages by the selected drum's
///        kind and the arp's mode.

#include "gui/ui/menu_view_of.h"
#include "project/device_settings.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/chains.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/nodes/roots.h"
#include "ui/scenes/menu/nodes/sound_arp.h"
#include "ui/scenes/menu/nodes/sound_randomizer.h"
#include "ui/scenes/menu/shortcut_overlay.h"
#include "ui/scenes/menu/shortcuts.h"

#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_drum.h"
#include "support/shortcut_layout.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <array>
#include <cstring>
#include <string>

#include "support/menu_context.h"
#include "support/spec_main.h"

namespace menu = deluge::ui::scenes::menu;
namespace spec = deluge::ui::scenes::spec;
using menu::MenuContext;

namespace {

/// @brief What a menu opened over @p f's clip edits; @p kit_global_fx as `context_for` takes it.
MenuContext context_of(const spec::SongFixture& f, bool kit_global_fx = false) {
	return spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, kit_global_fx);
}

bool arp_rate_relevant(const MenuContext& ctx) {
	return menu::bindings::relevant(menu::nodes::arp_rate, ctx);
}

/// @brief @p f's kit, affect-entire on, as the clip view's overview asks it: the kit's own chain, with whether it
///        edits the kit's own effects the latch's @p latched.
MenuContext kit_table_context(spec::SongFixture& f, bool latched) {
	f.clip->affectEntire = true;
	MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
	ctx.is_kit_global_fx = latched;
	return ctx;
}

} // namespace

// clang-format off
describe arp_rate_relevance("the sound arpeggiator's rate: where it is relevant", $ {
	it("is relevant on a synth", _ {
		expect(arp_rate_relevant(context_of(spec::make_synth_song()))).to_be_true();
	});

	it("is relevant on a kit row with a sound drum", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		expect(arp_rate_relevant(context_of(f))).to_be_true();
	});

	it("is not relevant on a kit's own effects", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		expect(arp_rate_relevant(context_of(f, /*kit_global_fx=*/true))).to_be_false();
	});

	it("is the kit's own table's (11,0), which the menu's overlay paints black and Shift + pad refuses", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		const MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
		expect(menu::shortcut_node(ctx.root, deluge::project::ShortcutsVersion::V3, false, 11, 0)).to_equal(&menu::nodes::arp_rate);
		const RGB pad = menu::shortcut_colour(ctx, deluge::project::ShortcutsVersion::V3, 11, 0);
		expect(pad.r).to_equal(0);
		expect(pad.g).to_equal(0);
		expect(pad.b).to_equal(0);
		expect(menu::shortcut_applies(menu::nodes::arp_rate, ctx)).to_be_false();
	});

	it("is relevant on a kit's own effects asked with the kit-global latch clear", _ {
		// The clip view's overview right after a menu close: the kit's own table and chain, but legacy's
		// `editingKitAffectEntire()` reads the latch, not the clip.
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
		ctx.is_kit_global_fx = false;
		expect(arp_rate_relevant(ctx)).to_be_true();
	});

	it("is not relevant on a kit's own effects asked with the latch clear and a MIDI drum selected", _ {
		// `editingNonAudioDrumRow()` reads the kit's selected drum, whatever the menu edits.
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		f.clip->affectEntire = true;
		MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
		ctx.is_kit_global_fx = false;
		expect(ctx.non_audio_drum_row).to_be_true();
		expect(arp_rate_relevant(ctx)).to_be_false();
	});

	it("is not relevant on a MIDI drum row", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		expect(arp_rate_relevant(context_of(f))).to_be_false();
	});

	it("is not relevant on a gate drum row", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<GateDrum>(f);
		expect(arp_rate_relevant(context_of(f))).to_be_false();
	});

	it("is not relevant on a kit's own effects asked with the latch clear and a gate drum selected", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<GateDrum>(f);
		f.clip->affectEntire = true;
		MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
		ctx.is_kit_global_fx = false;
		expect(ctx.non_audio_drum_row).to_be_true();
		expect(arp_rate_relevant(ctx)).to_be_false();
	});

	it("is not relevant on a MIDI clip", _ {
		expect(arp_rate_relevant(context_of(spec::make_midi_song()))).to_be_false();
	});

	it("is not relevant on a CV clip", _ {
		expect(arp_rate_relevant(context_of(spec::make_cv_song()))).to_be_false();
	});
});

describe arp_kit_table_relevance("the kit's own shortcut table's arp and randomizer items: where they are relevant", $ {
	it("asks the arp octaves of the latch: relevant with it clear, not with it set", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, kit_table_context(f, false))).to_be_true();
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, kit_table_context(f, true))).to_be_false();
		// The kit's own menu is latched: its overlay paints (11,3) black and Shift + pad refuses it.
		const MenuContext menu_ctx = kit_table_context(f, true);
		expect(menu::shortcut_node(menu_ctx.root, deluge::project::ShortcutsVersion::V3, false, 11, 3)).to_equal(&menu::nodes::arp_octaves);
		const RGB pad = menu::shortcut_colour(menu_ctx, deluge::project::ShortcutsVersion::V3, 11, 3);
		expect(pad.r).to_equal(0);
		expect(pad.g).to_equal(0);
		expect(pad.b).to_equal(0);
		expect(menu::shortcut_applies(menu::nodes::arp_octaves, menu_ctx)).to_be_false();
	});

	it("does not ask the arp octaves on a gate drum row, latched or not", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<GateDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, kit_table_context(f, false))).to_be_false();
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, context_of(f))).to_be_false();
	});

	it("asks the arp octaves on a MIDI drum row with the latch clear", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, kit_table_context(f, false))).to_be_true();
	});

	it("keeps the arp gate and the velocity spread off with a MIDI drum selected", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_gate, kit_table_context(f, true))).to_be_false();
		expect(menu::bindings::relevant(menu::nodes::spread_velocity, kit_table_context(f, true))).to_be_false();
	});

	it("keeps the arp gate and the velocity spread on with a sound drum selected", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_gate, kit_table_context(f, true))).to_be_true();
		expect(menu::bindings::relevant(menu::nodes::spread_velocity, kit_table_context(f, true))).to_be_true();
	});
});

describe arp_kit_global_drum_kinds("the kit's own arp and randomizer columns with each drum kind selected, as main's", $ {
	it("hides the randomizer's param columns with a MIDI drum selected, as RandomizerUnpatchedParam", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		const MenuContext ctx = kit_table_context(f, /*latched=*/true);
		menu::bindings::write(menu::nodes::arp_mode, ctx, 1);
		expect(menu::bindings::relevant(menu::nodes::ratchet_amount, ctx)).to_be_false();
		expect(menu::bindings::relevant(menu::nodes::reverse_probability, ctx)).to_be_false();
	});

	it("hides the arp's rhythm and sequence length with a gate drum selected, as Rhythm and ArpUnpatchedParam", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<GateDrum>(f);
		const MenuContext ctx = kit_table_context(f, /*latched=*/true);
		expect(menu::bindings::relevant(menu::nodes::arp_rhythm, ctx)).to_be_false();
		expect(menu::bindings::relevant(menu::nodes::arp_sequence_length, ctx)).to_be_false();
	});

	it("hides the step repeat with a gate drum selected and keeps it with a MIDI drum, as StepRepeat", _ {
		spec::SongFixture gate = spec::make_kit_song();
		(void)spec::add_drum<GateDrum>(gate);
		expect(menu::bindings::relevant(menu::nodes::arp_step_repeat, kit_table_context(gate, true))).to_be_false();
		spec::SongFixture midi = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(midi);
		expect(menu::bindings::relevant(menu::nodes::arp_step_repeat, kit_table_context(midi, true))).to_be_true();
	});

	it("answers every column as main's isRelevant does, for a sound, MIDI and gate drum, arp off and on", _ {
		// The kit's own effects (`editingKitAffectEntire()`), where main's `editingNonAudioDrumRow()`,
		// `editingMidiDrumRow()` and `editingGateDrumRow()` still read the kit's selected drum
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), and `currentArpSettings` is the kit's own arp.
		struct Facts {
			bool non_audio, midi, gate, arp;
		};
		using Rule = bool (*)(const Facts&);
		struct Column {
			const char* name;
			const menu::Node* node;
			Rule main;
		};
		namespace nodes = menu::nodes;
		// `randomizer::RandomizerUnpatchedParam`: no non-audio row; the three global ids without the arp.
		constexpr Rule unpatched = [](const Facts& s) { return !s.non_audio && s.arp; };
		constexpr Rule unpatched_global = [](const Facts& s) { return !s.non_audio; };
		// `randomizer::midi_cv::RandomizerNonSoundInteger`: a non-audio row with the arp on.
		constexpr Rule non_sound = [](const Facts& s) { return s.non_audio && s.arp; };
		// The `midi_cv` items that ask `editingMidiDrumRow()` with the arp on.
		constexpr Rule midi_row = [](const Facts& s) { return s.midi && s.arp; };
		constexpr Rule never = [](const Facts&) { return false; };
		const std::array columns{
		    Column{"spread_gate", &nodes::spread_gate, unpatched},
		    Column{"spread_gate_midi", &nodes::spread_gate_midi, non_sound},
		    // `RandomizerSoundOnlyUnpatchedParam`: never on the kit's own effects.
		    Column{"spread_octave", &nodes::spread_octave, never},
		    Column{"spread_octave_midi", &nodes::spread_octave_midi, midi_row},
		    Column{"spread_velocity", &nodes::spread_velocity, unpatched_global},
		    // `midi_cv::SpreadVelocity`: a MIDI row, arp or not.
		    Column{"spread_velocity_midi", &nodes::spread_velocity_midi, [](const Facts& s) { return s.midi; }},
		    Column{"ratchet_amount", &nodes::ratchet_amount, unpatched},
		    Column{"ratchet_amount_midi", &nodes::ratchet_amount_midi, non_sound},
		    Column{"ratchet_probability", &nodes::ratchet_probability, unpatched},
		    Column{"ratchet_probability_midi", &nodes::ratchet_probability_midi, non_sound},
		    // `RandomizerNonKitSoundUnpatchedParam` and the chord `midi_cv` items: never on a kit.
		    Column{"chord_polyphony", &nodes::chord_polyphony, never},
		    Column{"chord_polyphony_midi", &nodes::chord_polyphony_midi, never},
		    Column{"chord_probability", &nodes::chord_probability, never},
		    Column{"chord_probability_midi", &nodes::chord_probability_midi, never},
		    Column{"note_probability", &nodes::note_probability, unpatched_global},
		    // `midi_cv::NoteProbability`: a non-audio row, arp or not.
		    Column{"note_probability_midi", &nodes::note_probability_midi, [](const Facts& s) { return s.non_audio; }},
		    Column{"swap_probability", &nodes::swap_probability, unpatched},
		    Column{"swap_probability_midi", &nodes::swap_probability_midi, midi_row},
		    Column{"bass_probability", &nodes::bass_probability, unpatched},
		    Column{"bass_probability_midi", &nodes::bass_probability_midi, midi_row},
		    Column{"glide_probability", &nodes::glide_probability, unpatched},
		    Column{"glide_probability_midi", &nodes::glide_probability_midi, midi_row},
		    Column{"reverse_probability", &nodes::reverse_probability, unpatched_global},
		    // The kit's own arp page: `ArpUnpatchedParam` (gate, sequence length) and `Rhythm` want no non-audio row;
		    // `StepRepeat` no gate row; `KitRate`, the mode, preset and sync always.
		    Column{"arp_gate", &nodes::arp_gate, unpatched_global},
		    Column{"arp_kit_rate", &nodes::arp_kit_rate, [](const Facts&) { return true; }},
		    Column{"arp_rhythm", &nodes::arp_rhythm, unpatched_global},
		    Column{"arp_sequence_length", &nodes::arp_sequence_length, unpatched_global},
		    Column{"arp_rhythm_midi", &nodes::arp_rhythm_midi, [](const Facts& s) { return s.non_audio; }},
		    Column{"arp_sequence_length_midi", &nodes::arp_sequence_length_midi,
		           [](const Facts& s) { return s.non_audio; }},
		    Column{"arp_step_repeat", &nodes::arp_step_repeat, [](const Facts& s) { return !s.gate; }},
		    // `Octaves`, `OctaveMode`, `ChordType`, `NoteModeForDrums`, `IncludeInKitArp`: a kit row's, never the kit's.
		    Column{"arp_octaves", &nodes::arp_octaves, never},
		    Column{"arp_octave_mode", &nodes::arp_octave_mode, never},
		    Column{"arp_chord_simulator", &nodes::arp_chord_simulator, never},
		    Column{"arp_note_mode_drums", &nodes::arp_note_mode_drums, never},
		    Column{"arp_include_in_kit_arp", &nodes::arp_include_in_kit_arp, never},
		    // `NoteMode`: anything but a kit row.
		    Column{"arp_note_mode", &nodes::arp_note_mode, [](const Facts&) { return true; }},
		};
		std::string wrong;
		const auto check = [&](spec::SongFixture& f, Facts facts, const char* kind) {
			const MenuContext ctx = kit_table_context(f, /*latched=*/true);
			menu::bindings::write(nodes::arp_mode, ctx, facts.arp ? 1 : 0);
			for (const Column& c : columns) {
				const bool ours = menu::bindings::relevant(*c.node, ctx);
				if (ours != c.main(facts)) {
					wrong += std::string{c.name} + " (" + kind + (facts.arp ? ", arp on" : ", arp off") + "): ours "
					         + (ours ? "relevant" : "hidden") + "\n";
				}
			}
		};
		for (const bool arp : {false, true}) {
			spec::SongFixture sound = spec::make_kit_song();
			(void)spec::add_drum<SoundDrum>(sound);
			check(sound, Facts{false, false, false, arp}, "sound drum");
			spec::SongFixture midi = spec::make_kit_song();
			(void)spec::add_drum<MIDIDrum>(midi);
			check(midi, Facts{true, true, false, arp}, "MIDI drum");
			spec::SongFixture gate = spec::make_kit_song();
			(void)spec::add_drum<GateDrum>(gate);
			check(gate, Facts{true, false, true, arp}, "gate drum");
		}
		expect(wrong).to_equal(std::string{});
	});

	it("keeps an edit to the kit's own MIDI/CV columns, which the kit's arp reads from its params on every render", _ {
		// Main's `midi_cv` items write the kit arp's fields, which `Kit::setupAndRenderArpPreOutput` overwrites from
		// the kit's params on the next render; the port writes the param itself, so the edit is what the render reads.
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		const ArpeggiatorSettings drum_before = *drum->getArpSettings();
		const MenuContext ctx = kit_table_context(f, /*latched=*/true);
		menu::bindings::write(menu::nodes::arp_mode, ctx, 1);
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		actionLogger.deleteAllLogs();
		menu::bindings::write(menu::nodes::spread_gate_midi, ctx, 30);
		Action* action = actionLogger.firstAction[BEFORE];
		expect(action).not_().to_be_null();
		if (action != nullptr) {
			expect(action->nextAction).to_be_null();
		}
		menu::bindings::write(menu::nodes::note_probability_midi, ctx, 20);

		// The kit's own render: `Kit::renderOutput` hands its clip's params to `setupAndRenderArpPreOutput`.
		auto* kit = static_cast<Kit*>(f.instrument);
		std::array<StereoSample, 16> buffer{};
		alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
		expect(kit->setActiveClip(setupModelStackWithSong(memory, f.song)->addTimelineCounter(f.clip),
		                          PgmChangeSend::NEVER))
		    .to_be_true();
		kit->renderOutput(setupModelStackWithSong(memory, f.song), buffer, nullptr, 0, 0, false, true);
		// The kit's arp is its active clip's (`Kit::getArpSettings`).
		const ArpeggiatorSettings& rendered = f.clip->arpSettings;
		expect(rendered.values.unsigned_word(deluge::arp::param::kSpreadGate)).to_equal(menu::bindings::menu_to_unsigned(30));
		expect(rendered.values.unsigned_word(deluge::arp::param::kNoteProbability)).to_equal(menu::bindings::menu_to_unsigned(20));
		expect(menu::bindings::read(menu::nodes::spread_gate_midi, ctx)).to_equal(30);
		expect(menu::bindings::read(menu::nodes::note_probability_midi, ctx)).to_equal(20);
		// The MIDI drum's own arp is not the kit's: its settings are untouched.
		expect(std::memcmp(&drum_before, drum->getArpSettings(), sizeof(ArpeggiatorSettings))).to_equal(0);

		// The spread gate write is one undo step, which restores the column.
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(menu::bindings::read(menu::nodes::spread_gate_midi, ctx)).not_().to_equal(30);
		actionLogger.deleteAllLogs();
		nullifyUIs();
	});
});
// clang-format on

DELUGE_SPEC(arp_rate_relevance_spec, arp_rate_relevance, arp_kit_table_relevance, arp_kit_global_drum_kinds)
