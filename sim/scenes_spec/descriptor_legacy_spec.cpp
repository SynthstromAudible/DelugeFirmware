/// @file
/// @brief Every setting row a menu shows, against the legacy menu item that shows it.
///
/// For each row with one legacy item, driven through `soundEditor.current*` as the sound editor drives it:
///   * the descriptor's menu range (`engine::menu_range`) is the item's `getMinValue`/`getMaxValue`, or its options'
///     count;
///   * `engine::to_menu` at the stored edges (and the values a family makes interesting) is the item's
///     `readCurrentValue` after the stored value is set through the row's Edit;
///   * `engine::from_menu` at the menu edges is what the item's `writeCurrentValue` stores.
///
/// A CHOICE row's menu lists the choices that apply in its context (`engine::setting_choice_applies`), in order, and
/// its menu value is a choice's position in that list. Each context-dependent list is driven in each of its contexts:
/// a track and a kit lane, ring mod, DX7's eligibility and the audio inputs.
///
/// A row with no single legacy item (a partner row, a row several items show together, a row only a gesture
/// changes) is a named skip, with its reason.
///
/// @note The SPEC may include `gui/menu_item/`; the library may not (`scripts/check_layer_rules.py`).

#include "support/descriptor_cases.h"

namespace {

/// Run @p cases, with the audio inputs unplugged.
void run(auto& self, const std::vector<Case>& cases) {
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Case& c : cases) {
		check(self, c);
	}
}

} // namespace

// clang-format off
describe descriptor_legacy_voice("The voice rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as polyphony, voice priority, voice count, sidechain send, synth mode and master transpose do", _ {
		run(self, voice_cases());
	});
	it("offers choke on a kit lane alone", _ {
		const SettingInfo& row = *deluge::engine::find_setting(deluge::instrument::kSettings, "polyphonic");
		expect(applicable(row, context_of(Where::SYNTH)).size()).to_equal(4u);
		expect(applicable(row, context_of(Where::KIT_LANE)).size()).to_equal(5u);
	});
});

describe descriptor_legacy_oscillators("The oscillator rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the oscillator and sample items do", _ {
		run(self, oscillator_cases());
	});
	it("lists the oscillator types as legacy does with the inputs plugged in, on a track and a kit lane", _ {
		AudioEngine::micPluggedIn = true;
		check(self, oscillator_cases()[0]);
		check(self, oscillator_cases()[1]);
		check(self, oscillator_cases()[3]);
		AudioEngine::micPluggedIn = false;
	});
	it("drops the sample and inputs under ring mod, and DX7 off a track's first oscillator", _ {
		const SettingInfo& row = *deluge::engine::find_setting(deluge::instrument::kSettings, "osc1/type");
		expect(list_of(applicable(row, context_of(Where::SYNTH_RINGMOD)))).to_equal(std::string{"0,1,2,3,4,5,6,"});
		expect(list_of(applicable(row, context_of(Where::SYNTH)))).to_equal(std::string{"0,1,2,3,4,5,6,7,8,9,"});
		expect(list_of(applicable(row, context_of(Where::SYNTH, 1)))).to_equal(std::string{"0,1,2,3,4,5,6,7,9,"});
		expect(list_of(applicable(row, context_of(Where::KIT_LANE)))).to_equal(std::string{"0,1,2,3,4,5,6,7,9,"});
		expect(list_of(applicable(row, SettingContext{.audio_inputs_present = true, .dx7_allowed = true})))
		    .to_equal(std::string{"0,1,2,3,4,5,6,7,8,9,10,11,"});
	});
});

describe descriptor_legacy_lfos_and_modulators("The LFO and FM modulator rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the LFO and modulator items do", _ {
		run(self, lfo_and_modulator_cases());
	});
});

describe descriptor_legacy_unison_and_midi("The unison and MIDI output rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the unison and MIDI output items do", _ {
		run(self, unison_and_midi_cases());
	});
});

describe descriptor_legacy_effects("The effect rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the filter, delay, sidechain, compressor and stutter items do", _ {
		run(self, effect_cases());
	});
});

describe descriptor_legacy_arp("The arp rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the arp and randomizer items do", _ {
		run(self, arp_cases());
	});
	it("ranges, reads and writes the host rows as a MIDI clip's arp and randomizer items do", _ {
		run(self, arp_host_cases());
	});
	it("offers the as-played and pattern note modes off a kit lane alone", _ {
		const SettingInfo& row = *deluge::engine::find_setting(deluge::engine::kArpSettingRows, "arpeggiator/noteMode");
		expect(applicable(row, context_of(Where::SYNTH)).size()).to_equal(9u);
		expect(applicable(row, context_of(Where::KIT_LANE)).size()).to_equal(7u);
	});
});

describe descriptor_legacy_model("The model groups' rows against legacy's items", $ {
	it("ranges, reads and writes as the reverb, swing, audio clip and clip value items do", _ {
		run(self, model_cases());
	});
});

describe descriptor_legacy_coverage("Every engine and model row with a menu", $ {
	it("is compared with its legacy item, or named as a skip with its reason", _ {
		std::set<std::string_view> covered;
		for (const Case* c : all_cases()) {
			covered.insert(c->path);
		}
		using deluge::project::SettingGroupId;
		const std::span<const SettingInfo> tables[] = {
		    deluge::instrument::kSettings,
		    deluge::effects::kEffectSettings,
		    deluge::engine::kArpSettingRows,
		    deluge::engine::kArpHostRows,
		    deluge::project::table_of(SettingGroupId::SONG),
		    deluge::project::table_of(SettingGroupId::AUDIO_CLIP),
		    deluge::project::table_of(SettingGroupId::CLIP_VALUES),
		};
		for (auto table : tables) {
			for (const SettingInfo& row : table) {
				const bool named = covered.contains(row.path) || skipped(row.path);
				expect(std::string{row.path} + (named ? " named" : " unnamed")).to_equal(std::string{row.path} + " named");
			}
		}
	});
	it("names each exception a coherence check must carry", _ {
		for (const Skip& exception : kExceptions) {
			expect(exception.reason.empty()).to_be_false();
		}
	});
});
// clang-format on

DELUGE_SPEC(descriptor_legacy_spec, descriptor_legacy_voice, descriptor_legacy_oscillators,
            descriptor_legacy_lfos_and_modulators, descriptor_legacy_unison_and_midi, descriptor_legacy_effects,
            descriptor_legacy_arp, descriptor_legacy_model, descriptor_legacy_coverage)
