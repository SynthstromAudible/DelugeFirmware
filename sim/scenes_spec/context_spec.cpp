#include "scenes/menu/context.h"

#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "scenes/menu/node.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Permission;
using Root = MenuContext::Root;

namespace {

/// @brief An ordinary patched param (LPF frequency) -- the shape the range rule must NOT fire on:
///        its value lives on the `Sound`, so legacy opens it through the base
///        `MenuItem::checkPermissionToBeginSession` on a multisampled synth without asking for a
///        range.
constexpr menu::Node param_node =
    menu::param(1, {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ});

/// @brief The same node marked range-dependent -- legacy's `MenuItem::isRangeDependent()`
///        (`gui/menu_item/menu_item.h:202`), the actual discriminator for spec E §4.1's rule.
constexpr menu::Node range_dependent_node = menu::with_range_dependent(param_node);

/// @brief A node with neither a bound param nor the range flag.
constexpr menu::Node submenu_node = menu::submenu(2, {});

int root_of(const MenuContext& ctx) {
	return static_cast<int>(ctx.root);
}

} // namespace

// clang-format off
describe menu_context("scenes::menu::context_for", $ {
	it("resolves a synth clip to the sound root, with the sound and the clip's param manager", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Sound));
		expect(ctx.resolved).to_be_true();
		expect(ctx.sound != nullptr).to_be_true();
		expect(ctx.params == &f.clip->paramManager).to_be_true();
		expect(ctx.mod == static_cast<ModControllableAudio*>(ctx.sound)).to_be_true();
		expect(ctx.affect_entire).to_be_false();
		expect(ctx.is_kit).to_be_false();
		expect(ctx.source == &ctx.sound->sources[0]).to_be_true();
		expect(ctx.sidechain == &ctx.mod->sidechain).to_be_true();
		expect(ctx.priority == &ctx.sound->voicePriority).to_be_true();
		expect(ctx.arp == &f.clip->arpSettings).to_be_true();
	});

	it("resolves a kit clip with kit_global_fx to the kit's own params and the kit-global-FX root", _ {
		auto f = spec::make_kit_song();
		spec::add_drum<SoundDrum>(f); // a selected drum the kit-global-FX branch must win over
		f.clip->affectEntire = true;
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/true);

		// NOTE: the task-3 brief's step-1 list says `Root::Sound` for this case. That is a slip in
		// the brief: `setup()` sends a kit with affectEntire to `soundEditorRootMenuKitGlobalFX`
		// (`sound_editor.cpp:1735-1737`), which is what `Root::KitGlobalFx` -- an enumerator the
		// brief's own struct declares, and which nothing else could ever produce -- exists for.
		// The port follows the legacy source; see the task-3 report.
		expect(root_of(ctx)).to_equal(static_cast<int>(Root::KitGlobalFx));
		expect(ctx.is_kit).to_be_true();
		expect(ctx.affect_entire).to_be_true();
		expect(ctx.is_kit_global_fx).to_be_true();
		expect(ctx.params == &f.clip->paramManager).to_be_true();
		expect(ctx.mod != nullptr).to_be_true();
		expect(ctx.sound == nullptr).to_be_true();
	});

	it("edits the selected row when kit_global_fx is false, even with affect-entire on", _ {
		// The kit-row grid-shortcut path (`sound_editor.cpp:1150-1158`): `setupKitGlobalFXMenu` is
		// false there while the clip's `affectEntire` is true, and legacy edits the row's params.
		// `affect_entire` still reports the clip's setting; `kit_global_fx` is what chose the branch.
		auto f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		expect(ctx.affect_entire).to_be_true();
		expect(ctx.is_kit_global_fx).to_be_false();
		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Drum));
		expect(ctx.sound == static_cast<Sound*>(drum)).to_be_true();
		expect(ctx.params == &f.clip->getNoteRowForDrum(drum)->paramManager).to_be_true();

		// ...and the model stack follows the same branch, not the no-note-row kit arm.
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = menu::model_stack_for(ctx, memory);
		expect(stack->getNoteRowAllowNull() == f.clip->getNoteRowForDrum(drum)).to_be_true();
	});

	it("resolves a kit clip with a selected sound drum to the drum root", _ {
		auto f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Drum));
		expect(ctx.is_kit).to_be_true();
		expect(ctx.is_drum).to_be_true();
		expect(ctx.sound == static_cast<Sound*>(drum)).to_be_true();
		expect(ctx.params == &f.clip->getNoteRowForDrum(drum)->paramManager).to_be_true();
		expect(ctx.arp == &drum->arpSettings).to_be_true();
	});

	it("resolves a kit clip with a selected MIDI drum to the MIDI-drum root, with only an arp", _ {
		auto f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::MidiDrum));
		expect(ctx.sound == nullptr).to_be_true();
		expect(ctx.arp == &drum->arpSettings).to_be_true();
	});

	it("refuses a kit clip with neither affect-entire nor a selected drum", _ {
		auto f = spec::make_kit_song();
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		expect(ctx.resolved).to_be_false();
		expect(ctx.params == nullptr).to_be_true();
	});

	it("resolves a MIDI instrument clip to the MIDI/CV root, with no sound", _ {
		auto f = spec::make_midi_song();
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::MidiOrCv));
		expect(ctx.sound == nullptr).to_be_true();
		expect(ctx.mod == nullptr).to_be_true();
		expect(ctx.params == &f.clip->paramManager).to_be_true();
	});

	it("resolves no clip and no session root to the settings root", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, nullptr, 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Settings));
		expect(ctx.clip == nullptr).to_be_true();
		expect(ctx.params == nullptr).to_be_true();
	});

	it("resolves the performance view to the performance root, with the song's own params", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, nullptr, 0, true, true, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Performance));
		expect(ctx.is_performance).to_be_true();
		expect(ctx.is_song_view).to_be_true();
		expect(ctx.params == &f.song->paramManager).to_be_true();
		expect(ctx.mod != nullptr).to_be_true();
	});

	it("resolves a session/arranger root with no clip to the song root", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, nullptr, 0, false, true, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Song));
		expect(ctx.params == &f.song->paramManager).to_be_true();
	});

	it("keeps the wide and narrow root-UI facts on separate fields, and picks the root from the wide one", _ {
		// `setup()` reads two different things: `!rootUIIsClipMinderScreen()` (`:1632`, `:1764-1767`),
		// which chooses the song root for the session, arranger AND song-automation views alike, and
		// `getRootUI() == &sessionView` (`gui/menu_item/song/midi_learn.h:35-37`), which only two
		// items in that root ask. `context_for` takes both and must not let either stand in for the
		// other -- so all four combinations are asserted, including the impossible one.
		auto f = spec::make_synth_song();

		const MenuContext arranger = menu::context_for(*f.song, nullptr, 0, false, /*session_or_arranger_root=*/true,
		                                              /*session_view_root=*/false, false);
		expect(root_of(arranger)).to_equal(static_cast<int>(Root::Song));
		expect(arranger.is_song_view).to_be_true();
		expect(arranger.is_session_view).to_be_false();

		const MenuContext session = menu::context_for(*f.song, nullptr, 0, false, /*session_or_arranger_root=*/true,
		                                             /*session_view_root=*/true, false);
		expect(root_of(session)).to_equal(static_cast<int>(Root::Song));
		expect(session.is_song_view).to_be_true();
		expect(session.is_session_view).to_be_true();
		// The narrow bool steers no branch: same root, same param manager, same mod controllable.
		expect(session.params == arranger.params).to_be_true();
		expect(session.mod == arranger.mod).to_be_true();

		// A clip minder: neither fact holds, and the narrow bool is not silently inherited.
		const MenuContext clip = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(clip.is_song_view).to_be_false();
		expect(clip.is_session_view).to_be_false();

		// The inconsistent pair a caller could write (`session_view_root` without the wide bool) is
		// transcribed, not repaired -- `context_for` polices no argument, and this pins that so the
		// day someone adds a repair they have to argue for it here.
		const MenuContext impossible = menu::context_for(*f.song, nullptr, 0, false, /*session_or_arranger_root=*/false,
		                                                /*session_view_root=*/true, false);
		expect(root_of(impossible)).to_equal(static_cast<int>(Root::Settings));
		expect(impossible.is_song_view).to_be_false();
		expect(impossible.is_session_view).to_be_true();
	});
});

describe menu_model_stack("scenes::menu::model_stack_for", $ {
	it("hands back a stack carrying the context's own mod controllable and param manager", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = menu::model_stack_for(ctx, memory);

		expect(stack->modControllable == static_cast<ModControllable*>(ctx.mod)).to_be_true();
		expect(stack->paramManager == static_cast<ParamManager*>(ctx.params)).to_be_true();
		expect(stack->song == f.song).to_be_true();
		expect(stack->getTimelineCounter() == static_cast<TimelineCounter*>(f.clip)).to_be_true();
	});

	it("uses the song as the timeline counter in a session/arranger root", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, nullptr, 0, false, true, false, /*kit_global_fx=*/false);

		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = menu::model_stack_for(ctx, memory);

		expect(stack->getTimelineCounter() == static_cast<TimelineCounter*>(f.song)).to_be_true();
		expect(stack->paramManager == static_cast<ParamManager*>(ctx.params)).to_be_true();
	});

	it("attaches the selected drum's note row for a kit", _ {
		auto f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = menu::model_stack_for(ctx, memory);

		expect(stack->modControllable == static_cast<ModControllable*>(ctx.mod)).to_be_true();
		expect(stack->paramManager == static_cast<ParamManager*>(ctx.params)).to_be_true();
		expect(stack->getNoteRowAllowNull() == f.clip->getNoteRowForDrum(drum)).to_be_true();
	});
});

describe menu_permission("scenes::menu::permission_for", $ {
	it("requires a range for a range-dependent node whose source has more than one, none selected", _ {
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);

		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		expect(ctx.range == nullptr).to_be_true();
		expect(static_cast<int>(menu::permission_for(range_dependent_node, ctx)))
		    .to_equal(static_cast<int>(Permission::MustSelectRange));
	});

	it("opens an ordinary patched param on the same multi-range sound without asking for a range", _ {
		// The base rule (`menu_item.cpp:27-31`): LPF frequency lives on the Sound, not in a range.
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);

		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		expect(param_node.range_dependent).to_be_false();
		expect(static_cast<int>(menu::permission_for(param_node, ctx))).to_equal(static_cast<int>(Permission::Yes));
	});

	it("allows a range-dependent node when the source has exactly one range, and selects it", _ {
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);

		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		expect(ctx.range == sound->sources[0].ranges.getElement(0)).to_be_true();
		expect(ctx.range_index).to_equal(static_cast<int16_t>(0));
		expect(static_cast<int>(menu::permission_for(range_dependent_node, ctx)))
		    .to_equal(static_cast<int>(Permission::Yes));
	});

	it("lets a range-dependent node open once a range has been picked into the context", _ {
		// The `:1955-1957` arm, unreachable straight out of context_for -- E2's picker is what
		// writes the chosen range back, and this is what it buys.
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);

		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		ctx.range = sound->sources[0].ranges.getElement(1);
		ctx.range_index = 1;
		expect(static_cast<int>(menu::permission_for(range_dependent_node, ctx)))
		    .to_equal(static_cast<int>(Permission::Yes));
	});

	it("never applies the range rule to a node with neither a bound param nor the flag", _ {
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);

		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		expect(static_cast<int>(menu::permission_for(submenu_node, ctx))).to_equal(static_cast<int>(Permission::Yes));
	});

	it("allows a range-dependent node on a kit row however many ranges it has", _ {
		auto f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		drum->sources[0].ranges.insertMultiRange(0);
		drum->sources[0].ranges.insertMultiRange(1);

		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
		expect(static_cast<int>(menu::permission_for(range_dependent_node, ctx)))
		    .to_equal(static_cast<int>(Permission::Yes));
	});

	it("refuses a node whose relevance predicate says no", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);

		menu::Node irrelevant = param_node;
		irrelevant.relevant = [](const MenuContext&) { return false; };
		expect(static_cast<int>(menu::permission_for(irrelevant, ctx))).to_equal(static_cast<int>(Permission::No));
	});
});
// clang-format on

/// Three `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead.
int context_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(menu_context, menu_model_stack, menu_permission).exec().is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
