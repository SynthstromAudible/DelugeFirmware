// clang-format off
#include "host/chains/pages.h"
#include "host/chains/params.h"
#include "effects/plugin.h"
#include "effects/setting_params.h"
#include "support/printers/param_id_printer.h"
#include "support/printers/plugin_id_printer.h"
#include "support/printers/remote_controls_printer.h"
#include <cppspec.hpp>

using namespace deluge;
namespace hc = deluge::host::chains::param;
namespace st = deluge::effects::setting;
using deluge::plugin::PressAction;
using deluge::host::chains::section_pages;

describe chain_pages("the host chains' pages", ${
	it("puts the volume over the pan on the first mod button", _{
		const auto pages = section_pages(0, {});
		expect(pages.size()).to_equal(size_t{1});
		expect(*pages[0].slots[0].param).to_equal(hc::kPan);
		expect(*pages[0].slots[1].param).to_equal(hc::kVolume);
	});
	it("cycles low-pass, high-pass, EQ from the upper knob, the lower stepping the mode", _{
		const auto pages = section_pages(1, {});
		expect(pages.size()).to_equal(size_t{3});
		expect(pages[0].slots[1].press).to_equal(PressAction::NEXT_PAGE);
		expect(*pages[0].slots[0].press_param).to_equal(st::kLpfMode);
		expect(*pages[1].slots[0].press_param).to_equal(st::kHpfMode);
		expect(pages[2].slots[0].press).to_equal(PressAction::NONE);
		expect(*pages[2].slots[1].param).to_equal(hc::kTreble);
		for (const auto& page : pages) {
			expect(page.slots[1].press).to_equal(PressAction::NEXT_PAGE);
		}
		expect(*pages[1].slots[1].param).to_equal(hc::kHpfFrequency);
	});
	it("has no page on the envelopes' button", _{
		expect(section_pages(2, {}).empty()).to_be_true();
	});
	it("steps the delay's settings, the alternative knobs the sync ones", _{
		auto pages = section_pages(3, {});
		expect(*pages[0].slots[1].press_param).to_equal(st::kDelayPingPong);
		expect(*pages[0].slots[0].press_param).to_equal(st::kDelayType);
		pages = section_pages(3, {.alt_delay = true});
		expect(*pages[0].slots[1].press_param).to_equal(st::kDelaySyncType);
		expect(*pages[0].slots[0].press_param).to_equal(st::kDelaySyncLevel);
	});
	it("gives a clip's reverb button the reverb preset and the sidechain", _{
		const auto pages = section_pages(4, {});
		expect(pages.size()).to_equal(size_t{1});
		expect(*pages[0].slots[0].param).to_equal(hc::kReverbAmount);
		expect(pages[0].slots[0].press).to_equal(PressAction::REVERB_PRESET);
		expect(*pages[0].slots[1].param).to_equal(hc::kSidechainCompressorVolume);
		expect(*pages[0].slots[1].press_param).to_equal(st::kSidechainSyncLevel);
	});
	it("gives the song a reverb group and a five-page compressor group", _{
		const auto pages = section_pages(4, {.song = true});
		expect(pages.size()).to_equal(size_t{6});
		expect(pages[0].group).to_equal(uint8_t{0});
		expect(*pages[0].slots[1].param).to_equal(st::kCompThreshold);
		expect(pages[0].slots[1].press).to_equal(PressAction::TOGGLE_GROUP);
		expect(pages[0].slots[0].press).to_equal(PressAction::REVERB_PRESET);
		expect(pages[0].page_id).to_equal(0u);
		for (uint32_t i = 1; i <= 5; i++) {
			expect(pages[i].group).to_equal(uint8_t{1});
			expect(pages[i].page_id).to_equal(i);
		}
		expect(*pages[1].slots[0].param).to_equal(st::kCompRatio);
		expect(*pages[5].slots[0].param).to_equal(st::kCompBlend);
		expect(pages[5].slots[0].press).to_equal(PressAction::NEXT_PAGE);
		expect(pages[5].slots[1].press).to_equal(PressAction::TOGGLE_GROUP);
	});
	it("gives the mod FX button the params its type has", _{
		auto pages = section_pages(5, {.mod_fx_type = effects::ModFXType::FLANGER});
		expect(pages.size()).to_equal(size_t{1});
		expect(pages[0].page_id).to_equal(1u);
		expect(*pages[0].slots[0].param).to_equal(hc::kModFXFeedback);
		expect(*pages[0].slots[1].param).to_equal(hc::kModFXRate);
		expect(*pages[0].slots[1].press_param).to_equal(st::kModFXType);
		expect(pages[0].slots[0].press).to_equal(PressAction::NEXT_PAGE);
		pages = section_pages(5, {.mod_fx_type = effects::ModFXType::CHORUS});
		expect(pages.size()).to_equal(size_t{2});
		expect(pages[1].page_id).to_equal(2u);
		expect(host::chains::mod_fx_page_valid(effects::ModFXType::GRAIN, 2)).to_be_true();
		expect(host::chains::mod_fx_page_valid(effects::ModFXType::FLANGER, 0)).to_be_false();
	});
	it("holds the stutter from the upper knob, and the clip's chain has the pitch below it", _{
		expect(section_pages(6, {})[0].slots[1].press).to_equal(PressAction::HOLD_STUTTER);
		expect(*section_pages(6, {})[0].slots[0].param).to_equal(hc::kPitchAdjust);
		expect(section_pages(6, {.song = true})[0].slots[0].param.has_value()).to_be_false();
	});
	it("gives each section's page by id alone, as its cycle holds it", _{
		for (uint32_t section = 0; section < 8; section++) {
			for (const bool song : {false, true}) {
				const host::chains::PageInputs in{.song = song, .mod_fx_type = effects::ModFXType::CHORUS};
				for (const auto& page : section_pages(section, in)) {
					expect(host::chains::section_page(section, page.page_id, in)).to_equal(page);
				}
			}
		}
		expect(host::chains::section_page(1, 3, {}).has_value()).to_be_false();
		expect(host::chains::section_page(4, 1, {}).has_value()).to_be_false();
		expect(host::chains::section_page(4, 6, {.song = true}).has_value()).to_be_false();
		expect(host::chains::section_page(5, 0, {.mod_fx_type = effects::ModFXType::FLANGER}).has_value())
		    .to_be_false();
	});
	it("puts the sample rate reduction over the bitcrush", _{
		const auto pages = section_pages(7, {});
		expect(*pages[0].slots[0].param).to_equal(hc::kBitcrushAmount);
		expect(*pages[0].slots[1].param).to_equal(hc::kSampleRateReduction);
	});
	it("sets keeps_stutter on a clip chain's button 4 page only, never the song's", _{
		for (uint32_t section = 0; section < 8; section++) {
			for (const bool song : {false, true}) {
				for (const auto& page : section_pages(section, {.song = song})) {
					expect(page.keeps_stutter).to_equal(section == 4 && !song);
				}
			}
		}
		expect(host::chains::mod_fx_page(1).keeps_stutter).to_be_false();
	});
	it("publishes its pages as a record keyed by the global effectable", _{
		const auto& record = host::chains::kRemoteControls;
		expect(record.plugin).to_equal(plugin::kGlobalEffectable);
		const std::array<plugin::RemoteControls, 1> records{record};
		expect(plugin::remote_controls_for(records, plugin::kGlobalEffectable)).to_equal(&records[0]);
		expect(plugin::remote_controls_for(records, plugin::kDelugeInstrument)).to_be_null();
	});
	it("gets and finds exactly the pages section_page gives, for every chain state", _{
		const auto& record = host::chains::kRemoteControls;
		const effects::ModFXType types[] = {effects::ModFXType::NONE,   effects::ModFXType::FLANGER,
		                                    effects::ModFXType::CHORUS, effects::ModFXType::PHASER,
		                                    effects::ModFXType::GRAIN,  effects::ModFXType::CHORUS_STEREO};
		for (const effects::ModFXType type : types) {
			effects::EffectStates states;
			states.mod_fx.type = type;
			const auto handle = plugin::StateHandle::of<effects::DelugeEffects>(states);
			for (const bool song : {false, true}) {
				for (const bool alt : {false, true}) {
					const plugin::PageContext ctx{.state = &handle, .song = song, .alt_delay = alt};
					const host::chains::PageInputs in{.song = song, .mod_fx_type = type, .alt_delay = alt};
					std::vector<plugin::RemotePage> expected;
					for (uint32_t section = 0; section < 8; section++) {
						for (const auto& page : section_pages(section, in)) {
							expected.push_back(page);
						}
					}
					expect(record.count(ctx)).to_equal(static_cast<uint32_t>(expected.size()));
					plugin::RemotePage out;
					for (uint32_t i = 0; i < expected.size(); i++) {
						expect(record.get(ctx, i, out)).to_be_true();
						expect(out).to_equal(expected[i]);
					}
					expect(record.get(ctx, static_cast<uint32_t>(expected.size()), out)).to_be_false();
					for (uint32_t section = 0; section < 8; section++) {
						for (uint32_t page_id = 0; page_id < 8; page_id++) {
							const bool lacking_view = section == 5; // mod_fx_page clamps a view past the last to feedback
							if (lacking_view) {
								expect(record.find(ctx, section, page_id, out)).to_be_true();
								expect(out).to_equal(host::chains::mod_fx_page(page_id));
							}
							else {
								const auto want = section_page(section, page_id, in);
								expect(record.find(ctx, section, page_id, out)).to_equal(want.has_value());
								if (want) {
									expect(out).to_equal(*want);
								}
							}
						}
					}
				}
			}
		}
	});
	it("decorates a param with the slot its page gives it, and a bare knob otherwise", _{
		const auto& record = host::chains::kRemoteControls;
		// A param on more than one page decorates as the first page that holds it, so every page of every context
		// (a song's or a clip's chain, either delay) must give a param the one slot.
		for (const bool song : {true, false}) {
			for (const bool alt_delay : {false, true}) {
				const plugin::PageContext ctx{.song = song, .alt_delay = alt_delay};
				for (uint32_t section = 0; section < 8; section++) {
					for (const auto& page : section_pages(section, {.song = song, .alt_delay = alt_delay})) {
						for (const auto& slot : page.slots) {
							if (slot.param) {
								expect(record.decorate(ctx, *slot.param)).to_equal(slot);
							}
						}
					}
				}
				expect(*record.decorate(ctx, hc::kModFXOffset).param).to_equal(hc::kModFXOffset);
			}
		}
	});
});
// clang-format on

CPPSPEC_SPEC(chain_pages)
