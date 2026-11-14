// clang-format off
#include "effects/global_effectable/pages.h"
#include "effects/global_effectable/params.h"
#include "effects/plugin.h"
#include "effects/setting_params.h"
#include <cppspec.hpp>

using namespace deluge;
namespace ge = deluge::global_effectable::param;
namespace st = deluge::effects::setting;
using deluge::plugin::PressAction;
using deluge::global_effectable::section_pages;

describe chain_pages("the host chains' pages", ${
	it("puts the volume over the pan on the first mod button", _{
		const auto pages = section_pages(0, {});
		expect(pages.size()).to_equal(size_t{1});
		expect(*pages[0].slots[0].param == ge::kPan).to_be_true();
		expect(*pages[0].slots[1].param == ge::kVolume).to_be_true();
	});
	it("cycles low-pass, high-pass, EQ from the upper knob, the lower stepping the mode", _{
		const auto pages = section_pages(1, {});
		expect(pages.size()).to_equal(size_t{3});
		expect(pages[0].slots[1].press == PressAction::NEXT_PAGE).to_be_true();
		expect(*pages[0].slots[0].press_param == st::kLpfMode).to_be_true();
		expect(*pages[1].slots[0].press_param == st::kHpfMode).to_be_true();
		expect(pages[2].slots[0].press == PressAction::NONE).to_be_true();
		expect(*pages[2].slots[1].param == ge::kTreble).to_be_true();
		for (const auto& page : pages) {
			expect(page.slots[1].press == PressAction::NEXT_PAGE).to_be_true();
			expect(page.slots[1].kind == plugin::ParamKind::UNPATCHED_GLOBAL).to_be_true();
		}
		expect(*pages[1].slots[1].param == ge::kHpfFrequency).to_be_true();
	});
	it("has no page on the envelopes' button", _{
		expect(section_pages(2, {}).empty()).to_be_true();
	});
	it("steps the delay's settings, the alternative knobs the sync ones", _{
		auto pages = section_pages(3, {});
		expect(*pages[0].slots[1].press_param == st::kDelayPingPong).to_be_true();
		expect(*pages[0].slots[0].press_param == st::kDelayType).to_be_true();
		pages = section_pages(3, {.alt_delay = true});
		expect(*pages[0].slots[1].press_param == st::kDelaySyncType).to_be_true();
		expect(*pages[0].slots[0].press_param == st::kDelaySyncLevel).to_be_true();
	});
	it("gives a clip's reverb button the reverb preset and the sidechain", _{
		const auto pages = section_pages(4, {});
		expect(pages.size()).to_equal(size_t{1});
		expect(*pages[0].slots[0].param == ge::kReverbAmount).to_be_true();
		expect(pages[0].slots[0].press == PressAction::REVERB_PRESET).to_be_true();
		expect(*pages[0].slots[1].param == ge::kSidechainCompressorVolume).to_be_true();
		expect(*pages[0].slots[1].press_param == st::kSidechainSyncLevel).to_be_true();
	});
	it("gives the song a reverb group and a five-page compressor group", _{
		const auto pages = section_pages(4, {.song = true});
		expect(pages.size()).to_equal(size_t{6});
		expect(pages[0].group).to_equal(uint8_t{0});
		expect(*pages[0].slots[1].param == st::kCompThreshold).to_be_true();
		expect(pages[0].slots[1].press == PressAction::TOGGLE_GROUP).to_be_true();
		expect(pages[0].slots[0].press == PressAction::REVERB_PRESET).to_be_true();
		expect(pages[0].page_id).to_equal(0u);
		for (uint32_t i = 1; i <= 5; i++) {
			expect(pages[i].group).to_equal(uint8_t{1});
			expect(pages[i].page_id).to_equal(i);
		}
		expect(*pages[1].slots[0].param == st::kCompRatio).to_be_true();
		expect(*pages[5].slots[0].param == st::kCompBlend).to_be_true();
		expect(pages[5].slots[0].press == PressAction::NEXT_PAGE).to_be_true();
		expect(pages[5].slots[1].press == PressAction::TOGGLE_GROUP).to_be_true();
	});
	it("gives the mod FX button the params its type has", _{
		auto pages = section_pages(5, {.mod_fx_type = effects::ModFXType::FLANGER});
		expect(pages.size()).to_equal(size_t{1});
		expect(pages[0].page_id).to_equal(1u);
		expect(*pages[0].slots[0].param == ge::kModFXFeedback).to_be_true();
		expect(*pages[0].slots[1].param == ge::kModFXRate).to_be_true();
		expect(*pages[0].slots[1].press_param == st::kModFXType).to_be_true();
		expect(pages[0].slots[0].press == PressAction::NEXT_PAGE).to_be_true();
		pages = section_pages(5, {.mod_fx_type = effects::ModFXType::CHORUS});
		expect(pages.size()).to_equal(size_t{2});
		expect(pages[1].page_id).to_equal(2u);
		expect(global_effectable::mod_fx_page_valid(effects::ModFXType::GRAIN, 2)).to_be_true();
		expect(global_effectable::mod_fx_page_valid(effects::ModFXType::FLANGER, 0)).to_be_false();
	});
	it("holds the stutter from the upper knob, and the clip's chain has the pitch below it", _{
		expect(section_pages(6, {})[0].slots[1].press == PressAction::HOLD_STUTTER).to_be_true();
		expect(*section_pages(6, {})[0].slots[0].param == ge::kPitchAdjust).to_be_true();
		expect(section_pages(6, {.song = true})[0].slots[0].param.has_value()).to_be_false();
	});
	it("gives each section's page by id alone, as its cycle holds it", _{
		for (uint32_t section = 0; section < 8; section++) {
			for (const bool song : {false, true}) {
				const global_effectable::PageInputs in{.song = song, .mod_fx_type = effects::ModFXType::CHORUS};
				for (const auto& page : section_pages(section, in)) {
					expect(global_effectable::section_page(section, page.page_id, in) == page).to_be_true();
				}
			}
		}
		expect(global_effectable::section_page(1, 3, {}).has_value()).to_be_false();
		expect(global_effectable::section_page(4, 1, {}).has_value()).to_be_false();
		expect(global_effectable::section_page(4, 6, {.song = true}).has_value()).to_be_false();
		expect(global_effectable::section_page(5, 0, {.mod_fx_type = effects::ModFXType::FLANGER}).has_value())
		    .to_be_false();
	});
	it("puts the sample rate reduction over the bitcrush", _{
		const auto pages = section_pages(7, {});
		expect(*pages[0].slots[0].param == ge::kBitcrushAmount).to_be_true();
		expect(*pages[0].slots[1].param == ge::kSampleRateReduction).to_be_true();
	});
	it("sets keeps_stutter on a clip chain's button 4 page only, never the song's", _{
		for (uint32_t section = 0; section < 8; section++) {
			for (const bool song : {false, true}) {
				for (const auto& page : section_pages(section, {.song = song})) {
					expect(page.keeps_stutter).to_equal(section == 4 && !song);
				}
			}
		}
		expect(global_effectable::mod_fx_page(1).keeps_stutter).to_be_false();
	});
	it("publishes its pages as a record keyed by the global effectable", _{
		const auto& record = global_effectable::kRemoteControls;
		expect(record.plugin == plugin::kGlobalEffectable).to_be_true();
		const std::array<plugin::RemoteControls, 1> records{record};
		expect(plugin::remote_controls_for(records, plugin::kGlobalEffectable) == &records[0]).to_be_true();
		expect(plugin::remote_controls_for(records, plugin::kDelugeInstrument) == nullptr).to_be_true();
	});
	it("gets and finds exactly the pages section_page gives, for every chain state", _{
		const auto& record = global_effectable::kRemoteControls;
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
					const global_effectable::PageInputs in{.song = song, .mod_fx_type = type, .alt_delay = alt};
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
						expect(out == expected[i]).to_be_true();
					}
					expect(record.get(ctx, static_cast<uint32_t>(expected.size()), out)).to_be_false();
					for (uint32_t section = 0; section < 8; section++) {
						for (uint32_t page_id = 0; page_id < 8; page_id++) {
							const auto want = section_page(section, page_id, in);
							const bool lacking_view = section == 5; // mod_fx_page clamps a view past the last to feedback
							if (lacking_view) {
								expect(record.find(ctx, section, page_id, out)).to_be_true();
								expect(out == global_effectable::mod_fx_page(page_id)).to_be_true();
							}
							else {
								expect(record.find(ctx, section, page_id, out)).to_equal(want.has_value());
								if (want) {
									expect(out == *want).to_be_true();
								}
							}
						}
					}
				}
			}
		}
	});
	it("decorates a param with the slot its page gives it, and a bare knob otherwise", _{
		const auto& record = global_effectable::kRemoteControls;
		const plugin::PageContext ctx{.song = true};
		for (uint32_t section = 0; section < 8; section++) {
			for (const auto& page : section_pages(section, {.song = true})) {
				for (const auto& slot : page.slots) {
					if (slot.param) {
						expect(record.decorate(ctx, *slot.param) == slot).to_be_true();
					}
				}
			}
		}
		expect(record.decorate(ctx, ge::kModFXOffset).kind == plugin::ParamKind::UNPATCHED_GLOBAL).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(chain_pages)
