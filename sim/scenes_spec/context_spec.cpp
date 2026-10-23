#include "gui/ui/menu_view_of.h"
#include "scenes/menu/context.h"

#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "scenes/menu/node.h"
#include "support/menu_engine.h"
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

/// @brief The slot of @p f's synth.
deluge::project::SlotAddress instrument_slot(const spec::SongFixture& f) {
	return {deluge::project::SlotAddress::Kind::OUTPUT, *f.instrument->id()};
}

/// @brief The slot of @p drum.
deluge::project::SlotAddress drum_slot(const SoundDrum& drum) {
	return {deluge::project::SlotAddress::Kind::DRUM, *drum.id()};
}

} // namespace

// clang-format off
describe menu_context("scenes::menu::context_for", $ {
	it("resolves a synth clip to the sound root, addressing the synth's slot and the clip", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Sound));
		expect(ctx.resolved).to_be_true();
		expect(ctx.addresses.slot == std::optional{instrument_slot(f)}).to_be_true();
		expect(ctx.addresses.clip == std::optional{deluge::project::ClipAddress{f.clip->id()}}).to_be_true();
		expect(deluge::scenes::spec::menu_engine(ctx).mod == static_cast<ModControllableAudio*>(static_cast<SoundInstrument*>(f.instrument))).to_be_true();
		expect(ctx.affect_entire).to_be_false();
		expect(ctx.is_kit).to_be_false();
		expect(ctx.source_index).to_equal(static_cast<int8_t>(0));
		expect(ctx.addresses.lane.has_value()).to_be_false();
	});

	it("resolves a kit clip with kit_global_fx to the kit's own chain and the kit-global-FX root", _ {
		auto f = spec::make_kit_song();
		spec::add_drum<SoundDrum>(f); // a selected drum the kit-global-FX branch must win over
		f.clip->affectEntire = true;
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/true);

		// NOTE: the task-3 brief's step-1 list says `Root::Sound` for this case. That is a slip in
		// the brief: `setup()` sends a kit with affectEntire to `soundEditorRootMenuKitGlobalFX`
		// (`sound_editor.cpp:1790-1792`), which is what `Root::KitGlobalFx` -- an enumerator the
		// brief's own struct declares, and which nothing else could ever produce -- exists for.
		// The port follows the legacy source; see the task-3 report.
		expect(root_of(ctx)).to_equal(static_cast<int>(Root::KitGlobalFx));
		expect(ctx.is_kit).to_be_true();
		expect(ctx.affect_entire).to_be_true();
		expect(ctx.is_kit_global_fx).to_be_true();
		expect(ctx.addresses.clip == std::optional{deluge::project::ClipAddress{f.clip->id()}}).to_be_true();
		expect(deluge::scenes::spec::menu_engine(ctx).mod != nullptr).to_be_true();
		expect(ctx.addresses.slot.has_value()).to_be_false();
		expect(ctx.addresses.chain.has_value() && ctx.addresses.chain_owner == bind::ChainOwner::KIT).to_be_true();
	});

	it("edits the selected row when kit_global_fx is false, even with affect-entire on", _ {
		// The kit-row grid-shortcut path (`sound_editor.cpp:1205-1213`): `setupKitGlobalFXMenu` is
		// false there while the clip's `affectEntire` is true, and legacy edits the row's params.
		// `affect_entire` still reports the clip's setting; `kit_global_fx` is what chose the branch.
		auto f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);

		expect(ctx.affect_entire).to_be_true();
		expect(ctx.is_kit_global_fx).to_be_false();
		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Drum));
		expect(ctx.addresses.slot == std::optional{drum_slot(*drum)}).to_be_true();
		expect(ctx.addresses.note_row.has_value()).to_be_true();
	});

	it("resolves a kit clip with a selected sound drum to the drum root", _ {
		auto f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Drum));
		expect(ctx.is_kit).to_be_true();
		expect(ctx.is_drum).to_be_true();
		expect(ctx.addresses.slot == std::optional{drum_slot(*drum)}).to_be_true();
		expect(ctx.addresses.note_row.has_value()).to_be_true();
		expect(ctx.addresses.lane.has_value() && ctx.addresses.lane->drum_id == *drum->id()).to_be_true();
	});

	it("resolves a kit clip with a selected MIDI drum to the MIDI-drum root, with only an arp", _ {
		auto f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::MidiDrum));
		expect(ctx.addresses.slot.has_value()).to_be_false();
		expect(ctx.addresses.lane.has_value() && ctx.addresses.lane->drum_id == *drum->id()).to_be_true();
	});

	it("refuses a kit clip with neither affect-entire nor a selected drum", _ {
		auto f = spec::make_kit_song();
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);

		expect(ctx.resolved).to_be_false();
		expect(ctx.addresses.slot.has_value() || ctx.addresses.chain.has_value()).to_be_false();
	});

	it("resolves a MIDI instrument clip to the MIDI/CV root, with no sound", _ {
		auto f = spec::make_midi_song();
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::MidiOrCv));
		expect(ctx.addresses.slot.has_value()).to_be_false();
		expect(deluge::scenes::spec::menu_engine(ctx).mod == nullptr).to_be_true();
		expect(ctx.addresses.clip == std::optional{deluge::project::ClipAddress{f.clip->id()}}).to_be_true();
	});

	it("resolves no clip and no session root to the settings root", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, nullptr), 0, false, false, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Settings));
		expect(ctx.addresses.clip.has_value()).to_be_false();
		expect(ctx.addresses.chain.has_value()).to_be_false();
	});

	it("resolves the performance view to the performance root, with the song's own chain", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, nullptr), 0, true, true, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Performance));
		expect(ctx.is_performance).to_be_true();
		expect(ctx.is_song_view).to_be_true();
		expect(ctx.addresses.chain.has_value() && ctx.addresses.chain_owner == bind::ChainOwner::SONG).to_be_true();
		expect(deluge::scenes::spec::menu_engine(ctx).mod != nullptr).to_be_true();
	});

	it("resolves a session/arranger root with no clip to the song root", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, nullptr), 0, false, true, false, /*kit_global_fx=*/false);

		expect(root_of(ctx)).to_equal(static_cast<int>(Root::Song));
		expect(ctx.addresses.chain.has_value() && ctx.addresses.chain_owner == bind::ChainOwner::SONG).to_be_true();
	});

	it("keeps the wide and narrow root-UI facts on separate fields, and picks the root from the wide one", _ {
		// `setup()` reads two different things: `!rootUIIsClipMinderScreen()` (`:1632`, `:1764-1767`),
		// which chooses the song root for the session, arranger AND song-automation views alike, and
		// `getRootUI() == &sessionView` (`gui/menu_item/song/midi_learn.h:35-37`), which only two
		// items in that root ask. `context_for` takes both and must not let either stand in for the
		// other -- so all four combinations are asserted, including the impossible one.
		auto f = spec::make_synth_song();

		const MenuContext arranger = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, nullptr), 0, false, /*session_or_arranger_root=*/true,
		                                              /*session_view_root=*/false, false);
		expect(root_of(arranger)).to_equal(static_cast<int>(Root::Song));
		expect(arranger.is_song_view).to_be_true();
		expect(arranger.is_session_view).to_be_false();

		const MenuContext session = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, nullptr), 0, false, /*session_or_arranger_root=*/true,
		                                             /*session_view_root=*/true, false);
		expect(root_of(session)).to_equal(static_cast<int>(Root::Song));
		expect(session.is_song_view).to_be_true();
		expect(session.is_session_view).to_be_true();
		// The narrow bool steers no branch: same root, same addresses, same mod controllable.
		expect(session.addresses == arranger.addresses).to_be_true();
		expect(deluge::scenes::spec::menu_engine(session).mod == deluge::scenes::spec::menu_engine(arranger).mod).to_be_true();

		// A clip minder: neither fact holds, and the narrow bool is not silently inherited.
		const MenuContext clip = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		expect(clip.is_song_view).to_be_false();
		expect(clip.is_session_view).to_be_false();

		// The inconsistent pair a caller could write (`session_view_root` without the wide bool) is
		// transcribed, not repaired -- `context_for` polices no argument, and this pins that so the
		// day someone adds a repair they have to argue for it here.
		const MenuContext impossible = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, nullptr), 0, false, /*session_or_arranger_root=*/false,
		                                                /*session_view_root=*/true, false);
		expect(root_of(impossible)).to_equal(static_cast<int>(Root::Settings));
		expect(impossible.is_song_view).to_be_false();
		expect(impossible.is_session_view).to_be_true();
	});
});

describe menu_permission("scenes::menu::permission_for", $ {
	it("requires a range for a range-dependent node whose source has more than one, none selected", _ {
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);
		deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them

		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);
		expect(ctx.range_index).to_equal(static_cast<int16_t>(-1));
		expect(static_cast<int>(menu::permission_for(range_dependent_node, ctx)))
		    .to_equal(static_cast<int>(Permission::MustSelectRange));
	});

	it("opens an ordinary patched param on the same multi-range sound without asking for a range", _ {
		// The base rule (`menu_item.cpp:27-31`): LPF frequency lives on the Sound, not in a range.
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);
		deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them

		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);
		expect(param_node.range_dependent).to_be_false();
		expect(static_cast<int>(menu::permission_for(param_node, ctx))).to_equal(static_cast<int>(Permission::Yes));
	});

	it("allows a range-dependent node when the source has exactly one range, and selects it", _ {
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them

		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);
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
		deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them

		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);
		ctx.range_index = 1;
		expect(static_cast<int>(menu::permission_for(range_dependent_node, ctx)))
		    .to_equal(static_cast<int>(Permission::Yes));
	});

	it("never applies the range rule to a node with neither a bound param nor the flag", _ {
		auto f = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(f.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);
		deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them

		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);
		expect(static_cast<int>(menu::permission_for(submenu_node, ctx))).to_equal(static_cast<int>(Permission::Yes));
	});

	it("allows a range-dependent node on a kit row however many ranges it has", _ {
		auto f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		drum->sources[0].ranges.insertMultiRange(0);
		drum->sources[0].ranges.insertMultiRange(1);
		deluge::edits::recordRanges(*drum, 0); // recorded, as a gesture records them

		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);
		expect(static_cast<int>(menu::permission_for(range_dependent_node, ctx)))
		    .to_equal(static_cast<int>(Permission::Yes));
	});

	it("refuses a node whose relevance predicate says no", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, /*kit_global_fx=*/false);

		menu::Node irrelevant = param_node;
		irrelevant.bind.target = {}; // a hook node: its predicate decides
		irrelevant.relevant = [](const MenuContext&) { return false; };
		expect(static_cast<int>(menu::permission_for(irrelevant, ctx))).to_equal(static_cast<int>(Permission::No));
	});
});
// clang-format on

DELUGE_SPEC(context_spec, menu_context, menu_permission)
