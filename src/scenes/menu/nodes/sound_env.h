#pragma once

/// @brief The four envelope subtrees as data: `env1` .. `env4`, each an ADSR page.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (fix round 1, I3). From
/// `gui/menu_item/generate/dmenus/envelope/{envelope,__init__}.py` and
/// `generate/g_menus.inc:159-202` (at `ce4a79de0`). Every leaf is an `envelope::Segment`
/// (`gui/menu_item/envelope/segment.h:24`), which is a `source::PatchedParam` -- a patched param
/// whose index is `p + source_id_` (`gui/menu_item/source/patched_param.h:21`), with the envelope
/// number playing the part of the source id. The generator therefore emits
/// `params::LOCAL_ENV_0_ATTACK` with an envelope index of 0..3 and the four attack params come out
/// consecutive (`modulation/params/param.h:112-115`), which is exactly what a `ParamRef` carries
/// directly here.
///
// clang-format off
/// | node | legacy global | param |
/// |---|---|---|
/// | `env<n>_attack` | `env<n>AttackMenu` | `LOCAL_ENV_<n-1>_ATTACK` |
/// | `env<n>_decay` | `env<n>DecayMenu` | `LOCAL_ENV_<n-1>_DECAY` |
/// | `env<n>_sustain` | `env<n>SustainMenu` | `LOCAL_ENV_<n-1>_SUSTAIN` |
/// | `env<n>_release` | `env<n>ReleaseMenu` | `LOCAL_ENV_<n-1>_RELEASE` |
/// | `env<n>` | `env<n>Menu` | -- (`envelope::EnvelopeMenu`, `envelope/envelope_menu.h:26`) |
// clang-format on
///
/// @note **Two known gaps, both `envelope::EnvelopeMenu`'s.** It overrides `renderMenuItems`
///       (`envelope_menu.h:37-104`) to draw one ADSR curve across the whole page instead of four
///       column meters -- a page-level renderer `Node` has no field for, so the four columns fall
///       back to their own hints (`Number`'s default knob: `Segment` overrides no
///       `getRenderingStyle`). And its name and title are a `FormattedTitle`
///       (`envelope_menu.h:31-34`) that substitutes the envelope number into
///       `STRING_FOR_ENVELOPE_MENU_TITLE`; a `Node::title` is one static string id, so all four
///       submenus carry the unsubstituted id. Both belong with E2's dynamic-label work.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains

#include <span>

namespace deluge::scenes::menu::nodes {

/// @brief One segment of envelope @p index.
///
/// `params::LOCAL_ENV_0_*` + @p index is `source::PatchedParam::getP()`
/// (`gui/menu_item/source/patched_param.h:28`) written out: the four envelopes' copies of each
/// parameter are consecutive enumerators (`modulation/params/param.h:86-89,112-123`).
constexpr Node envelope_segment(uint16_t name, uint16_t title, int32_t first_param, uint8_t index) {
	return with_patch_chain(
	    param(name, {deluge::modulation::params::Kind::PATCHED, static_cast<uint8_t>(first_param + index)}, Hint::Knob,
	          nullptr, title));
}

/// @brief One envelope page. A macro rather than a function because the four `Node`s have to be
///        distinct objects with static storage for their addresses to go in a `children` array --
///        a `constexpr` factory returning an aggregate cannot hand back pointers into itself.
#define DELUGE_ENVELOPE(n, index)                                                                                      \
	inline constexpr Node env##n##_attack = envelope_segment(                                                          \
	    sid(deluge::l10n::String::STRING_FOR_ATTACK), sid(deluge::l10n::String::STRING_FOR_ENV_ATTACK_MENU_TITLE),     \
	    deluge::modulation::params::LOCAL_ENV_0_ATTACK, (index));                                                      \
	inline constexpr Node env##n##_decay = envelope_segment(                                                           \
	    sid(deluge::l10n::String::STRING_FOR_DECAY), sid(deluge::l10n::String::STRING_FOR_ENV_DECAY_MENU_TITLE),       \
	    deluge::modulation::params::LOCAL_ENV_0_DECAY, (index));                                                       \
	inline constexpr Node env##n##_sustain = envelope_segment(                                                         \
	    sid(deluge::l10n::String::STRING_FOR_SUSTAIN), sid(deluge::l10n::String::STRING_FOR_ENV_SUSTAIN_MENU_TITLE),   \
	    deluge::modulation::params::LOCAL_ENV_0_SUSTAIN, (index));                                                     \
	inline constexpr Node env##n##_release = envelope_segment(                                                         \
	    sid(deluge::l10n::String::STRING_FOR_RELEASE), sid(deluge::l10n::String::STRING_FOR_ENV_RELEASE_MENU_TITLE),   \
	    deluge::modulation::params::LOCAL_ENV_0_RELEASE, (index));                                                     \
	inline constexpr const Node* kEnv##n##Children[] = {&env##n##_attack, &env##n##_decay, &env##n##_sustain,          \
	                                                    &env##n##_release};                                            \
	inline constexpr Node env##n =                                                                                     \
	    submenu(sid(deluge::l10n::String::STRING_FOR_ENVELOPE_MENU_TITLE), kEnv##n##Children, Layout::Horizontal)

DELUGE_ENVELOPE(1, 0);
DELUGE_ENVELOPE(2, 1);
DELUGE_ENVELOPE(3, 2);
DELUGE_ENVELOPE(4, 3);

#undef DELUGE_ENVELOPE

/// @brief This file's four contributions to `kBatch1Roots`: `env1` .. `env4`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundEnvChildren;

} // namespace deluge::scenes::menu::nodes
