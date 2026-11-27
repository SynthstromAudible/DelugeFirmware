/// @file
/// @brief Every setting row a menu shows, against the reference rows of the menu item that shows it.
///
/// For each row with one item, over `tests/golden/ui/menu_oracle/descriptors.tsv` (what upstream `main`'s item answered
/// when the sound editor drove it through its `current*` members):
///   * the descriptor's menu range (`plugin::menu_range`) is the item's least and greatest value, or its options'
///     count;
///   * `plugin::to_menu` at the stored edges (and the values a family makes interesting) is the item's reading after
///     the stored value is set through the row's Edit;
///   * `plugin::from_menu` at the menu edges is what the item's write stored.
///
/// A CHOICE row's menu lists the choices that apply in its context (`plugin::setting_choice_applies`), in order, and
/// its menu value is a choice's position in that list. Each context-dependent list is driven in each of its contexts:
/// a track and a kit lane, ring mod, DX7's eligibility and the audio inputs.
///
/// A row with no single item (a partner row, a row several items show together, a row only a gesture changes) is a
/// named skip, with its reason.
///
/// Whether a row's menu shows (`plugin::setting_applies`) is the menu's: over a set of fixtures (each synth mode,
/// polyphony and oscillator type, a kit's sound, MIDI and gate rows and the whole kit, MIDI and CV tracks, an audio
/// clip and the song), a row applies exactly where one of its items was shown, an item being shown when it and every
/// submenu over it in the root the editor opens there was relevant.

#include "support/descriptor_fixtures.h"
#include "support/menu_engine.h"

#include <algorithm>
#include <optional>

namespace {

/// Run @p cases, with the audio inputs unplugged.
void run(auto& self, const std::vector<Case>& cases) {
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Case& c : cases) {
		check(self, c);
	}
}

std::string verdict(std::string_view fixture, std::string_view path, bool applies) {
	return std::string{fixture} + ": " + std::string{path} + (applies ? " shows" : " does not show");
}

/// The same for a frozen `legacy_shows` cell: `1`, `0`, or what a missing row prints as.
std::string frozen_verdict(std::string_view fixture, std::string_view path, std::string_view cell) {
	return cell == "1" || cell == "0" ? verdict(fixture, path, cell == "1")
	                                  : std::string{fixture} + ": " + std::string{path} + " " + std::string{cell};
}

} // namespace

// clang-format off
describe descriptor_oracle_voice("The voice rows' descriptors against the items' rows", $ {
	it("ranges, reads and writes as polyphony, voice priority, voice count, sidechain send, synth mode and master transpose do", _ {
		run(self, voice_cases());
	});
	it("offers choke on a kit lane alone", _ {
		const SettingInfo& row = *deluge::plugin::find_setting(deluge::instruments::classic::kSettings, "polyphonic");
		expect(applicable(row, Where::SYNTH, Family::INSTRUMENT).size()).to_equal(4u);
		expect(applicable(row, Where::KIT_LANE, Family::INSTRUMENT).size()).to_equal(5u);
	});
});

describe descriptor_oracle_oscillators("The oscillator rows' descriptors against the items' rows", $ {
	it("ranges, reads and writes as the oscillator and sample items do", _ {
		run(self, oscillator_cases());
	});
	it("lists the oscillator types as the item's rows do with the inputs plugged in, on a track and a kit lane", _ {
		AudioEngine::micPluggedIn = true;
		check(self, oscillator_cases()[0], "@plugged");
		check(self, oscillator_cases()[1], "@plugged");
		check(self, oscillator_cases()[3], "@plugged");
		AudioEngine::micPluggedIn = false;
	});
	it("drops the sample and inputs under ring mod, and DX7 off both oscillators (it is a sound's engine)", _ {
		const SettingInfo& row = *deluge::plugin::find_setting(deluge::instruments::classic::kSettings, "osc1/type");
		expect(list_of(applicable(row, Where::SYNTH_RINGMOD, Family::INSTRUMENT))).to_equal(std::string{"0,1,2,3,4,5,6,7,"});
		expect(list_of(applicable(row, Where::SYNTH, Family::INSTRUMENT))).to_equal(std::string{"0,1,2,3,4,5,6,7,8,10,"});
		const SettingInfo& second = *deluge::plugin::find_setting(deluge::instruments::classic::kSettings, "osc2/type");
		expect(list_of(applicable(second, Where::SYNTH, Family::INSTRUMENT))).to_equal(std::string{"0,1,2,3,4,5,6,7,8,10,"});
		expect(list_of(applicable(row, Where::KIT_LANE, Family::INSTRUMENT))).to_equal(std::string{"0,1,2,3,4,5,6,7,8,10,"});
		const MenuContext& ctx = enter(Where::SYNTH);
		SettingContext plugged = context_of(Where::SYNTH, Family::INSTRUMENT);
		plugged.audio_inputs_present = true;
		expect(list_of(applicable(row, plugged, ctx, Where::SYNTH)))
		    .to_equal(std::string{"0,1,2,3,4,5,6,7,8,10,11,12,"});
	});
});

describe descriptor_oracle_lfos_and_modulators("The LFO and FM modulator rows' descriptors against the items' rows", $ {
	it("ranges, reads and writes as the LFO and modulator items do", _ {
		run(self, lfo_and_modulator_cases());
	});
});

describe descriptor_oracle_unison_and_midi("The unison and MIDI output rows' descriptors against the items' rows", $ {
	it("ranges, reads and writes as the unison and MIDI output items do", _ {
		run(self, unison_and_midi_cases());
	});
});

describe descriptor_oracle_effects("The effect rows' descriptors against the items' rows", $ {
	it("ranges, reads and writes as the filter, delay, sidechain, compressor and stutter items do", _ {
		run(self, effect_cases());
	});
});

describe descriptor_oracle_arp("The arp rows' descriptors against the items' rows", $ {
	it("ranges, reads and writes as the arp and randomizer items do", _ {
		run(self, arp_cases());
	});
	it("ranges, reads and writes the host rows as a MIDI clip's arp and randomizer items do", _ {
		run(self, arp_host_cases());
	});
	it("offers the as-played and pattern note modes off a kit lane alone", _ {
		const SettingInfo& row = *deluge::plugin::find_setting(deluge::plugin::kArpSettingRows, "arpeggiator/noteMode");
		expect(applicable(row, Where::SYNTH, Family::ARP).size()).to_equal(9u);
		expect(applicable(row, Where::KIT_LANE, Family::ARP).size()).to_equal(7u);
	});
});

describe descriptor_oracle_model("The model groups' rows against the items' rows", $ {
	it("ranges, reads and writes as the reverb, swing, audio clip and clip value items do", _ {
		run(self, model_cases());
	});
	it("reads and writes the ducking volume as its stepped item does", _ {
		AudioEngine::micPluggedIn = false;
		AudioEngine::lineInPluggedIn = false;
		for (const Case& c : scaled_cases()) {
			check_scaled(self, c);
		}
	});
});

describe descriptor_oracle_applicability("Where each row's menu shows, against the reference relevance", $ {
	it("shows each row exactly where the reference shows one of its items, in every fixture", _ {
		AudioEngine::micPluggedIn = false;
		AudioEngine::lineInPluggedIn = false;
		std::set<const AppliesException*> reached;
		for (const Fixture& fixture : fixtures()) {
			enter(fixture.where);
			fixture.setup(settle(fixture.where), song_of(fixture.where));
			const MenuContext& ctx = settle(fixture.where);
			for (Family family : fixture.families) {
				const SettingContext descriptor_ctx = descriptor_context(fixture, ctx, family);
				const SpecReader owner{ctx, fixture.where, family};
				for (const SettingInfo& row : table_of(family)) {
					if (std::ranges::any_of(port_cases(), [&](const Case& own) {
						    return own.family == family && own.path == row.path;
					    })) {
						continue; // the port's own row: main had no item to show it (settings_spec holds where it applies)
					}
					if (!has_item(family, row.path)) {
						// A row no item shows says nothing of where it shows, but for a partner, which follows its row.
						// A sound's effect rows are compared as the EFFECT family's.
						const SettingInfo* effect_row = deluge::plugin::find_setting(deluge::effects::kEffectSettings, row.path);
						const bool effect = family == Family::INSTRUMENT && effect_row != nullptr && effect_row->extras == row.extras;
						const bool silent = effect || row.extras == nullptr
						                    || (row.extras->applies_when.empty() && row.extras->applies_any.empty()
						                        && row.extras->choice_when.empty());
						expect(std::string{row.path} + (silent || is_partner(family, row.path) ? " silent" : " has conditions"))
						    .to_equal(std::string{row.path} + " silent");
						continue;
					}
					const std::string shows_at = "applies/" + std::string{fixture.name} + "/" + std::string{name_of(family)} + "/" + std::string{row.path};
					const std::string frozen_shows = cell(shows_at, "legacy_shows");
					const bool applies = deluge::plugin::setting_applies(row, descriptor_ctx, owner.reader());
					if (const AppliesException* known = applies_exception(family, fixture.name, row.path)) {
						// A named difference: the menu's verdict is the one named and the row's is the opposite.
						reached.insert(known);
						expect(frozen_verdict(fixture.name, row.path, frozen_shows)).to_equal(verdict(fixture.name, row.path, known->legacy_shows));
						expect(verdict(fixture.name, row.path, applies)).to_equal(verdict(fixture.name, row.path, !known->legacy_shows));
						continue;
					}
					expect(verdict(fixture.name, row.path, applies)).to_equal(frozen_verdict(fixture.name, row.path, frozen_shows));
				}
				expect(std::string{fixture.name} + " reads " + (owner.unknown.empty() ? "" : owner.unknown.front()))
				    .to_equal(std::string{fixture.name} + " reads ");
			}
			unload_files(ctx, song_of(fixture.where));
		}
		for (const AppliesException& exception : kAppliesExceptions) {
			const std::string at = std::string{exception.path} + " in " + std::string{exception.fixture};
			expect(exception.reason.size()).to_be_greater_than(size_t{0});
			expect(at + (reached.contains(&exception) ? " reached" : " never reached")).to_equal(at + " reached");
		}
	});
	it("shows a partner row exactly where its row shows, in every fixture", _ {
		for (const Fixture& fixture : fixtures()) {
			enter(fixture.where);
			fixture.setup(settle(fixture.where), song_of(fixture.where));
			const MenuContext& ctx = settle(fixture.where);
			for (Family family : fixture.families) {
				const SettingContext descriptor_ctx = descriptor_context(fixture, ctx, family);
				const SpecReader owner{ctx, fixture.where, family};
				for (const SettingInfo& row : table_of(family)) {
					const std::string_view partner_path = deluge::plugin::mapping_of(row).partner_path();
					const SettingInfo* partner = partner_path.empty() ? nullptr : deluge::plugin::find_setting(table_of(family), partner_path);
					if (partner == nullptr) {
						continue;
					}
					const bool applies = deluge::plugin::setting_applies(row, descriptor_ctx, owner.reader());
					expect(verdict(fixture.name, partner->path, deluge::plugin::setting_applies(*partner, descriptor_ctx, owner.reader())))
					    .to_equal(verdict(fixture.name, partner->path, applies));
				}
			}
			unload_files(ctx, song_of(fixture.where));
		}
	});
	it("offers the whole kit's sounds no DX7 oscillator, as no sound's oscillator plays one", _ {
		// main offered DX7 there (`osc::Type::mayUseDx`); the DX7 is a sound's engine, the synth mode's to choose.
		const SettingInfo& row = *deluge::plugin::find_setting(deluge::instruments::classic::kSettings, "osc1/type");
		const MenuContext& ctx = enter(Where::SYNTH);
		const SettingContext whole_kit{.owner = Owner::KIT, .instrument = SettingContext::Instrument::KIT};
		expect(list_of(applicable(row, whole_kit, ctx, Where::SYNTH))).to_equal(std::string{"0,1,2,3,4,5,6,7,8,10,"});
	});
});

describe descriptor_oracle_validity("Every table's conditions", $ {
	it("name rows of their own table, with values those rows store, and their display mappings are well formed, in "
	   "every setting group and the arp's",
	   _ {
		// Every id a SettingGroupId can hold: a group added later is walked with the rest.
		using deluge::project::SettingGroupId;
		for (int32_t id = 0; id <= std::numeric_limits<uint8_t>::max(); ++id) {
			const std::span<const SettingInfo> table = deluge::project::table_of(static_cast<SettingGroupId>(id));
			if (table.empty()) {
				continue;
			}
			expect("group " + std::to_string(id) + (deluge::plugin::conditions_are_valid(table) ? " valid" : " invalid"))
			    .to_equal("group " + std::to_string(id) + " valid");
			// The run-time tables (the song's, the instrument device's) are out of reach of a static_assert.
			expect("group " + std::to_string(id)
			       + (deluge::plugin::mappings_are_valid(table) ? " mappings valid" : " mappings invalid"))
			    .to_equal("group " + std::to_string(id) + " mappings valid");
		}
		expect(deluge::plugin::conditions_are_valid(deluge::plugin::kArpRows)).to_be_true();
		expect(deluge::plugin::conditions_are_valid(deluge::instruments::classic::kSettings)).to_be_true();
		expect(deluge::plugin::conditions_are_valid(deluge::effects::kEffectSettings)).to_be_true();
	});
});

describe descriptor_oracle_coverage("Every engine and model row with a menu", $ {
	it("is compared with its item's rows, or named as a skip with its reason", _ {
		std::set<std::string_view> covered;
		for (const Case* c : all_cases()) {
			covered.insert(c->path);
		}
		for (const Case& c : port_cases()) {
			covered.insert(c.path);
		}
		using deluge::project::SettingGroupId;
		struct OwnedRows {
			NamesOwner owner;
			std::span<const SettingInfo> rows;
		};
		const OwnedRows tables[] = {
		    {deluge::plugin::kDelugeInstrument, deluge::instruments::classic::kSettings},
		    {deluge::plugin::kDelugeInstrument, deluge::effects::kEffectSettings},
		    {deluge::plugin::kDelugeInstrument, deluge::plugin::kArpSettingRows},
		    {SettingGroupId::CLIP_ARP, deluge::plugin::kArpHostRows},
		    {SettingGroupId::SONG, deluge::project::table_of(SettingGroupId::SONG)},
		    {SettingGroupId::AUDIO_CLIP, deluge::project::table_of(SettingGroupId::AUDIO_CLIP)},
		    {SettingGroupId::CLIP_VALUES, deluge::project::table_of(SettingGroupId::CLIP_VALUES)},
		};
		for (const auto& [owner, rows] : tables) {
			for (const SettingInfo& row : rows) {
				const bool named = covered.contains(row.path) || skipped(owner, row.path);
				expect(std::string{row.path} + (named ? " named" : " unnamed")).to_equal(std::string{row.path} + " named");
			}
		}
	});
	it("names an exception only while its row still differs from its item's rows", _ {
		for (const Exception& exception : kExceptions) {
			const std::string at = std::string{exception.path} + " exception";
			expect(exception.reason.size()).to_be_greater_than(size_t{0});
			expect(at + (exception.differs() ? " differs" : " agrees")).to_equal(at + " differs");
		}
	});
});

describe descriptor_oracle_reference_rows("descriptor_oracle_spec: the reference rows", $ {
	it("parses descriptors.tsv clean and asks every row of its range, read, write, applicability and probe columns", _ {
		spec::OracleTable& t = spec::OracleTable::named("descriptors");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{3000});
		expect(t.unasked({"range", "read/", "write/", "legacy_shows", "probe"})).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(descriptor_oracle_spec, descriptor_oracle_voice, descriptor_oracle_oscillators,
            descriptor_oracle_lfos_and_modulators, descriptor_oracle_unison_and_midi, descriptor_oracle_effects,
            descriptor_oracle_arp, descriptor_oracle_model, descriptor_oracle_applicability, descriptor_oracle_validity,
            descriptor_oracle_coverage, descriptor_oracle_reference_rows)
