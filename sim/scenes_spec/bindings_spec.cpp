/// @file
/// @brief Task 4's spec: `menu::bindings` read/write/range/names/learn against a REAL model.
///
/// The scaling assertions deliberately call the legacy `computeFinalValueForStandardMenuItem` /
/// `computeCurrentValueForStandardMenuItem` / `...ForPan` directly (task-4 ruling 1: the spec may
/// include `gui/menu_item/`, the library may not), so the transcription in `bindings.cpp` is
/// checked against the original arithmetic rather than against a second copy of itself.

#include "scenes/menu/bindings.h"

#include "scenes/menu/context.h"
#include "scenes/menu/node.h"

#include "gui/l10n/l10n.h"
#include "gui/menu_item/value_scaling.h" // ruling 1: the SPEC may include gui/menu_item/, the library may not
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "notify/dirty_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <string>

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
namespace params = deluge::modulation::params;
namespace bindings = deluge::scenes::menu::bindings;

namespace {

/// @brief An ordinary patched param -- legacy `patched_param::Integer` (`patched_param/integer.cpp`).
constexpr Node lpf_freq_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_FREQUENCY),
                                           {params::Kind::PATCHED, params::LOCAL_LPF_FREQ});

/// @brief An unpatched (sound) param -- legacy `menu_item::UnpatchedParam` (`unpatched_param.cpp`).
constexpr Node bass_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_BASS),
                                       {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS});

/// @brief An unpatched GLOBAL param, on a kit clip's own (global-effectable) param manager.
constexpr Node global_volume_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_VOLUME_LEVEL),
                                                {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME});

/// @brief Legacy `patched_param::Pan` (`patched_param/pan.h:26-28`): the relative range and the pan
///        scaling, here left at `Scaling::Standard` so `scaling_for`'s `Hint::Pan` /
///        `params::isParamPan` backstop is the thing under test.
constexpr Node pan_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_PAN),
                                      {params::Kind::PATCHED, params::LOCAL_PAN}, menu::Hint::Pan);

/// @brief The same param with `Scaling::Pan` named explicitly and no `Hint::Pan` -- the declared
///        route, which must reach the same arithmetic as the inferred one above.
constexpr Node pan_declared_node =
    menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_PAN), {params::Kind::PATCHED, params::LOCAL_PAN},
                menu::Hint::Knob, nullptr, /*title=*/0, menu::Scaling::Pan);

/// @brief Legacy `osc::PulseWidth` (`osc/pulse_width.h:32,34-37`) -- a patched param on
///        `Scaling::HalfPrecision`, which has no inferable spelling and must be declared.
constexpr Node pulse_width_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_PULSE_WIDTH),
                                              {params::Kind::PATCHED, params::LOCAL_OSC_A_PHASE_WIDTH},
                                              menu::Hint::Knob, nullptr, /*title=*/0, menu::Scaling::HalfPrecision);

/// @brief Legacy `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:29-33`) -- the
///        unpatched half of the same scaling.
constexpr Node comp_threshold_node =
    menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_THRESHOLD),
                {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_COMPRESSOR_THRESHOLD}, menu::Hint::Knob, nullptr,
                /*title=*/0, menu::Scaling::HalfPrecision);

/// @brief The `Kind::Enum` names table -- two real l10n ids.
constexpr uint16_t kEnumNames[] = {
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_DISABLED),
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ENABLED),
};

/// @brief Storage the `toggle_item` / `enumeration` get/set hooks below drive, since a `GetFn` /
///        `SetFn` is a plain function pointer and cannot capture.
bool g_toggle_state = false;
int32_t g_enum_state = 0;

constexpr Node toggle_node = menu::toggle_item(
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ENABLED),
    [](const MenuContext&) { return static_cast<int32_t>(g_toggle_state); },
    [](const MenuContext&, int32_t v) { g_toggle_state = v != 0; });

constexpr Node enum_node = menu::enumeration(
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_LPF), kEnumNames,
    [](const MenuContext&) { return g_enum_state; }, [](const MenuContext&, int32_t v) { g_enum_state = v; });

/// @brief What the feedback hook last saw.
const Node* g_feedback_node = nullptr;
int32_t g_feedback_knob_pos = -1;
int32_t g_feedback_calls = 0;

void record_feedback(const MenuContext&, const Node& node, int32_t knob_pos) {
	g_feedback_node = &node;
	g_feedback_knob_pos = knob_pos;
	++g_feedback_calls;
}

/// @brief Install `record_feedback` for one example and take it back down however the example ends.
///
/// A bare `set_feedback_hook(nullptr)` at the end of the body leaks the hook into every later
/// example if an assertion above it fails (review round 1, test-quality nit).
struct FeedbackHookGuard {
	FeedbackHookGuard() {
		g_feedback_node = nullptr;
		g_feedback_knob_pos = -1;
		g_feedback_calls = 0;
		bindings::set_feedback_hook(&record_feedback);
	}
	~FeedbackHookGuard() { bindings::set_feedback_hook(nullptr); }
};

/// @brief Drop whatever earlier specs raised, so a `has()` below is about this write only.
void clear_dirty() {
	(void)deluge::notify::dirty().drain();
}

MenuContext synth_context(spec::SongFixture& f) {
	return menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
}

int32_t patched_raw(const MenuContext& ctx, params::ParamType p) {
	return ctx.params->getPatchedParamSet()->getValue(p);
}

int32_t unpatched_raw(const MenuContext& ctx, params::ParamType p) {
	return ctx.params->getUnpatchedParamSet()->getValue(p);
}

/// @brief A `SoundDrum` added to the kit with **no note row** -- `spec::add_drum` minus its
///        `createNewNoteRowForKit` half. The affect-entire loop must step over it
///        (`bindings.cpp`'s `note_row == nullptr` skip) rather than dereference the null row legacy
///        would have (`model_stack.cpp:116`).
SoundDrum* add_drum_without_note_row(spec::SongFixture& f) {
	auto* kit = static_cast<Kit*>(f.instrument);
	auto* drum = spec::detail::place<SoundDrum>();

	ParamManagerForTimeline params;
	params.setupWithPatching();
	Sound::initParams(&params);
	drum->setupAsSample(&params);
	f.song->backUpParamManager(drum, f.clip, &params, true);

	kit->addDrum(drum); // appends -- `model/instrument/kit.cpp:422-430`
	return drum;
}

} // namespace

// clang-format off
describe menu_bindings_value("scenes::menu::bindings read/write", $ {
	before_all([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("round-trips a patched param and stores exactly what legacy's getFinalValue would have", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		bindings::write(lpf_freq_node, ctx, 30);

		expect(bindings::read(lpf_freq_node, ctx)).to_equal(30);
		expect(patched_raw(ctx, params::LOCAL_LPF_FREQ)).to_equal(computeFinalValueForStandardMenuItem(30));
	});

	it("reads a patched param through the legacy current-value scaling", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		bindings::write(lpf_freq_node, ctx, 17);
		const int32_t raw = patched_raw(ctx, params::LOCAL_LPF_FREQ);
		expect(bindings::read(lpf_freq_node, ctx)).to_equal(computeCurrentValueForStandardMenuItem(raw));
	});

	it("round-trips an unpatched sound param", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		bindings::write(bass_node, ctx, 12);

		expect(bindings::read(bass_node, ctx)).to_equal(12);
		expect(unpatched_raw(ctx, params::UNPATCHED_BASS)).to_equal(computeFinalValueForStandardMenuItem(12));
	});

	it("round-trips an unpatched global param on a kit's own param manager", _ {
		auto f = spec::make_kit_song();
		f.clip->affectEntire = true;
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/true);

		bindings::write(global_volume_node, ctx, 41);

		expect(bindings::read(global_volume_node, ctx)).to_equal(41);
		expect(unpatched_raw(ctx, params::UNPATCHED_VOLUME)).to_equal(computeFinalValueForStandardMenuItem(41));
	});

	it("uses the pan scaling, not the standard one, for a Hint::Pan node", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		bindings::write(pan_node, ctx, -10);

		expect(patched_raw(ctx, params::LOCAL_PAN)).to_equal(computeFinalValueForPan(-10));
		expect(bindings::read(pan_node, ctx)).to_equal(-10);
	});

	it("reaches the same pan arithmetic whether Scaling::Pan is declared or inferred", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		bindings::write(pan_declared_node, ctx, -10);
		expect(patched_raw(ctx, params::LOCAL_PAN)).to_equal(computeFinalValueForPan(-10));
		expect(bindings::read(pan_declared_node, ctx)).to_equal(-10);
		expect(bindings::read(pan_node, ctx)).to_equal(-10);
	});

	it("round-trips a toggle_item through its own get/set", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		g_toggle_state = false;

		expect(bindings::read(toggle_node, ctx)).to_equal(0);
		bindings::write(toggle_node, ctx, 1);
		expect(g_toggle_state).to_be_true();
		expect(bindings::read(toggle_node, ctx)).to_equal(1);
	});
});

describe menu_bindings_scaling("scenes::menu::bindings value scaling", $ {
	// Fix round 1, Important 2 + Important 3(b): all three `Scaling` enumerators, at BOTH range ends
	// and a mid value, each against the legacy function. The ends are the only place the arithmetic
	// is not a formula (`value_scaling.cpp:22-33`, `:35-46`, `:48-58` each special-case min and max),
	// so they are the only place a transcription slip could hide.

	it("Scaling::Standard matches the legacy standard pair at 0, 25 and 50", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		for (int32_t v : {0, 25, 50}) {
			bindings::write(lpf_freq_node, ctx, v);
			expect(patched_raw(ctx, params::LOCAL_LPF_FREQ)).to_equal(computeFinalValueForStandardMenuItem(v));
			expect(bindings::read(lpf_freq_node, ctx))
			    .to_equal(computeCurrentValueForStandardMenuItem(computeFinalValueForStandardMenuItem(v)));
		}
	});

	it("Scaling::Pan matches the legacy pan pair at -25, 0 and 25", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		for (int32_t v : {-25, 0, 25}) {
			bindings::write(pan_declared_node, ctx, v);
			expect(patched_raw(ctx, params::LOCAL_PAN)).to_equal(computeFinalValueForPan(v));
			expect(bindings::read(pan_declared_node, ctx))
			    .to_equal(computeCurrentValueForPan(computeFinalValueForPan(v)));
		}
	});

	it("Scaling::HalfPrecision matches the legacy half-precision pair at 0, 25 and 50, patched", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		for (int32_t v : {0, 25, 50}) {
			bindings::write(pulse_width_node, ctx, v);
			expect(patched_raw(ctx, params::LOCAL_OSC_A_PHASE_WIDTH))
			    .to_equal(computeFinalValueForHalfPrecisionMenuItem(v));
			expect(bindings::read(pulse_width_node, ctx))
			    .to_equal(computeCurrentValueForHalfPrecisionMenuItem(computeFinalValueForHalfPrecisionMenuItem(v)));
		}
	});

	it("Scaling::HalfPrecision works on an unpatched param too", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		bindings::write(comp_threshold_node, ctx, 40);
		expect(unpatched_raw(ctx, params::UNPATCHED_COMPRESSOR_THRESHOLD))
		    .to_equal(computeFinalValueForHalfPrecisionMenuItem(40));
	});

	it("never stores a negative value for a half-precision param, where Standard would", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		// The whole point of `computeFinalValueForHalfPrecisionMenuItem` -- "comp params and osc
		// pulse width aren't set up for negative inputs" (`value_scaling.cpp:36`). Menu 0 stores
		// INT32_MIN under Standard and 0 under HalfPrecision.
		bindings::write(pulse_width_node, ctx, 0);
		expect(patched_raw(ctx, params::LOCAL_OSC_A_PHASE_WIDTH)).to_equal(0);
		expect(computeFinalValueForStandardMenuItem(0) < 0).to_be_true();
	});

	it("gives a half-precision param the ordinary 0..50 range", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		// Neither `osc::PulseWidth` nor `CompParam` overrides getMaxValue/getMinValue.
		const menu::IntRange r = bindings::range(pulse_width_node, ctx);
		expect(r.min).to_equal(0);
		expect(r.max).to_equal(50);
	});
});

describe menu_bindings_guards("scenes::menu::bindings guards", $ {
	// Fix round 1, Important 3(a): the three refusals this port added beyond legacy.

	it("reads 0 and writes nothing when the param manager cannot serve the param", _ {
		// A MIDI clip's param manager is `setupMIDI()` -- a MIDIParamCollection in summaries[0] and
		// nothing in summaries[1]. Legacy's `getPatchedParamSet()` freezes (E411) in alpha/beta and
		// casts null otherwise (`param_manager.h:128-135`); `getPatchedAutoParamFromId` dereferences
		// the same null summary (`model_stack.cpp:245-256`).
		auto f = spec::make_midi_song();
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		expect(ctx.params == &f.clip->paramManager).to_be_true();
		clear_dirty();

		expect(bindings::read(lpf_freq_node, ctx)).to_equal(0);
		bindings::write(lpf_freq_node, ctx, 30); // must not crash
		expect(bindings::read(lpf_freq_node, ctx)).to_equal(0);
		// Refused, so nothing is owed a repaint either.
		expect(deluge::notify::dirty().drain().has(deluge::notify::Dirty::ParamAutomation)).to_be_false();
	});

	it("writes nothing when the context has no model stack", _ {
		// `model_stack_for`'s precondition (`context.h`): resolved, and either the song view or a
		// clip. A context with neither would build a stack over a null timeline counter.
		auto f = spec::make_synth_song();
		MenuContext ctx = synth_context(f);
		ParamManagerForTimeline* params = ctx.params;
		const int32_t before = params->getPatchedParamSet()->getValue(params::LOCAL_LPF_FREQ);

		ctx.clip = nullptr;
		ctx.is_song_view = false;
		clear_dirty();

		bindings::write(lpf_freq_node, ctx, 30); // must not crash

		expect(params->getPatchedParamSet()->getValue(params::LOCAL_LPF_FREQ)).to_equal(before);
		expect(deluge::notify::dirty().drain().has(deluge::notify::Dirty::ParamAutomation)).to_be_false();
	});

	it("skips a kit drum with no note row and still writes the ones that have rows", _ {
		auto f = spec::make_kit_song();
		SoundDrum* with_row = spec::add_drum<SoundDrum>(f);
		SoundDrum* orphan = add_drum_without_note_row(f);

		int32_t row_index = 0;
		expect(f.clip->getNoteRowForDrum(orphan, &row_index) == nullptr).to_be_true();

		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		ctx.affect_entire_held = true;

		bindings::write(lpf_freq_node, ctx, 33); // must not crash on the row-less drum

		NoteRow* row = f.clip->getNoteRowForDrum(with_row, &row_index);
		expect(row != nullptr).to_be_true();
		expect(row->paramManager.getPatchedParamSet()->getValue(params::LOCAL_LPF_FREQ))
		    .to_equal(computeFinalValueForStandardMenuItem(33));
	});
});

describe menu_bindings_notify("scenes::menu::bindings notify + feedback", $ {
	it("raises ParamAutomation, ModTarget and GridMain for a param write", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		clear_dirty();

		bindings::write(lpf_freq_node, ctx, 22);

		const deluge::notify::DirtySet d = deluge::notify::dirty().drain();
		expect(d.has(deluge::notify::Dirty::ParamAutomation)).to_be_true();
		expect(d.has(deluge::notify::Dirty::ModTarget)).to_be_true();
		// The pad half of `possiblyRefreshAutomationEditorGrid` (`automation_view.cpp:624`'s
		// `uiNeedsRendering(&automationView)`): `ParamAutomation`/`ModTarget` reach only `r.oled`
		// (`notify_drain_mapping.cpp:20`), so without GridMain the automation grid would stop
		// repainting (fix round 1, Important 1).
		expect(d.has(deluge::notify::Dirty::GridMain)).to_be_true();
	});

	it("raises Oled for a get/set write", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		clear_dirty();

		bindings::write(toggle_node, ctx, 1);

		const deluge::notify::DirtySet d = deluge::notify::dirty().drain();
		expect(d.has(deluge::notify::Dirty::Oled)).to_be_true();
		// The get/set path is not a param write: no grid, no automation.
		expect(d.has(deluge::notify::Dirty::GridMain)).to_be_false();
		expect(d.has(deluge::notify::Dirty::ParamAutomation)).to_be_false();
	});

	it("raises nothing for a node with neither a param nor a set hook", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const Node unwritable = menu::submenu(1, {}); // no bind.param, no bind.set
		clear_dirty();

		bindings::write(unwritable, ctx, 1);

		expect(deluge::notify::dirty().drain().any()).to_be_false();
	});

	it("calls the feedback hook after a param write, with the knob position", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const FeedbackHookGuard hook;

		bindings::write(lpf_freq_node, ctx, 25);

		expect(g_feedback_calls).to_equal(1);
		expect(g_feedback_node == &lpf_freq_node).to_be_true();
		// `paramValueToKnobPos` reports -64..64 for a 0..50 menu value; 25 is the midpoint.
		expect(g_feedback_knob_pos).to_equal(0);
	});

	it("does not call a cleared feedback hook", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		bindings::set_feedback_hook(nullptr);
		g_feedback_calls = 0;
		bindings::write(lpf_freq_node, ctx, 9);
		expect(g_feedback_calls).to_equal(0);
	});
});

describe menu_bindings_affect_entire("scenes::menu::bindings kit affect-entire", $ {
	it("writes every SoundDrum in the kit's firstDrum chain", _ {
		auto f = spec::make_kit_song();
		SoundDrum* first = spec::add_drum<SoundDrum>(f);
		SoundDrum* second = spec::add_drum<SoundDrum>(f);

		// The kit-row context (`kit_global_fx=false`), with the affect-entire button held --
		// legacy `unpatched_param.cpp:52`'s
		// `currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()`.
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		ctx.affect_entire_held = true;

		bindings::write(lpf_freq_node, ctx, 33);

		const int32_t expected = computeFinalValueForStandardMenuItem(33);
		auto* kit = static_cast<Kit*>(f.instrument);
		int32_t drums_seen = 0;
		for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
			int32_t row_index = 0;
			NoteRow* row = f.clip->getNoteRowForDrum(drum, &row_index);
			expect(row != nullptr).to_be_true();
			expect(row->paramManager.getPatchedParamSet()->getValue(params::LOCAL_LPF_FREQ)).to_equal(expected);
			++drums_seen;
		}
		expect(drums_seen).to_equal(2);
		expect(first != second).to_be_true();
	});

	it("writes only the selected drum when affect-entire is not held", _ {
		auto f = spec::make_kit_song();
		spec::add_drum<SoundDrum>(f);
		SoundDrum* second = spec::add_drum<SoundDrum>(f); // add_drum leaves the last one selected

		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		expect(ctx.sound == static_cast<Sound*>(second)).to_be_true();

		auto* kit = static_cast<Kit*>(f.instrument);
		auto row_value = [&](Drum* drum) {
			int32_t row_index = 0;
			NoteRow* row = f.clip->getNoteRowForDrum(drum, &row_index);
			return row->paramManager.getPatchedParamSet()->getValue(params::LOCAL_LPF_FREQ);
		};
		const int32_t untouched_before = row_value(kit->firstDrum);

		bindings::write(lpf_freq_node, ctx, 44);

		expect(row_value(static_cast<Drum*>(second))).to_equal(computeFinalValueForStandardMenuItem(44));
		expect(row_value(kit->firstDrum)).to_equal(untouched_before);
		expect(kit->firstDrum != static_cast<Drum*>(second)).to_be_true();
	});
});

describe menu_bindings_range_names("scenes::menu::bindings range/names/learn", $ {
	before_all([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("gives a standard param the legacy 0..50 default", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const menu::IntRange r = bindings::range(lpf_freq_node, ctx);
		expect(r.min).to_equal(0);
		expect(r.max).to_equal(50);
		expect(r.step).to_equal(1);
	});

	it("gives a pan node the legacy -25..25 default", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const menu::IntRange r = bindings::range(pan_node, ctx);
		expect(r.min).to_equal(-25);
		expect(r.max).to_equal(25);
		expect(r.step).to_equal(1);
	});

	it("gives an enum node 0..names-1", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const menu::IntRange r = bindings::range(enum_node, ctx);
		expect(r.min).to_equal(0);
		expect(r.max).to_equal(1);
	});

	it("gives a toggle node 0..1", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const menu::IntRange r = bindings::range(toggle_node, ctx);
		expect(r.min).to_equal(0);
		expect(r.max).to_equal(1);
	});

	it("prefers fixed_range over the per-kind default", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const Node n = menu::int_item(1, [](const MenuContext&) { return 0; }, [](const MenuContext&, int32_t) {},
		                              menu::IntRange{3, 9, 2});
		const menu::IntRange r = bindings::range(n, ctx);
		expect(r.min).to_equal(3);
		expect(r.max).to_equal(9);
		expect(r.step).to_equal(2);
	});

	it("prefers a dynamic range over fixed_range", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		Node n = menu::int_item(1, [](const MenuContext&) { return 0; }, [](const MenuContext&, int32_t) {},
		                        menu::IntRange{3, 9, 2});
		n.bind.range = [](const MenuContext&) { return menu::IntRange{-1, 1, 1}; };
		const menu::IntRange r = bindings::range(n, ctx);
		expect(r.min).to_equal(-1);
		expect(r.max).to_equal(1);
	});

	it("resolves enum names through the profile's text_or_digits", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		std::string_view out[4]{};
		deluge::ui::shell::TextScratch scratch[4]{};
		const size_t n = bindings::names(enum_node, ctx, out, scratch);

		expect(static_cast<int>(n)).to_equal(2);
		expect(std::string(out[0]))
		    .to_equal(std::string(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_DISABLED)));
		expect(std::string(out[1]))
		    .to_equal(std::string(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_ENABLED)));
	});

	it("fills a dynamic_names node from its own hook", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		const Node n = menu::dynamic_enumeration(
		    1,
		    [](const MenuContext&, std::span<std::string_view> out) -> size_t {
			    out[0] = "one";
			    out[1] = "two";
			    out[2] = "three";
			    return 3;
		    },
		    [](const MenuContext&) { return 0; }, [](const MenuContext&, int32_t) {});

		std::string_view out[4]{};
		deluge::ui::shell::TextScratch scratch[4]{};
		expect(static_cast<int>(bindings::names(n, ctx, out, scratch))).to_equal(3);
		expect(std::string(out[2])).to_equal(std::string("three"));
	});

	it("builds the learn descriptor legacy builds, patched and unpatched", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		ParamDescriptor patched;
		patched.setToHaveParamOnly(params::LOCAL_LPF_FREQ); // patched_param.cpp:51-55
		expect(bindings::learn_target(lpf_freq_node, ctx) == patched).to_be_true();

		ParamDescriptor unpatched;
		unpatched.setToHaveParamOnly(params::UNPATCHED_BASS + params::UNPATCHED_START); // unpatched_param.cpp:88-92
		expect(bindings::learn_target(bass_node, ctx) == unpatched).to_be_true();

		expect(bindings::learn_target(toggle_node, ctx).isNull()).to_be_true();
	});
});
// clang-format on

/// Six `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead.
int bindings_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(menu_bindings_value, menu_bindings_scaling, menu_bindings_guards, menu_bindings_notify,
	                          menu_bindings_affect_entire, menu_bindings_range_names)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
