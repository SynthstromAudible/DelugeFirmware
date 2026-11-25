#pragma once

/// @brief The four envelope subtrees as data: `env1` .. `env4`, each a page of four segments, then its settings.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From `origin/main:src/deluge/gui/ui/menus.cpp`'s
/// envelope block, from `env1AttackMenu`. Every leaf is an `envelope::Segment`
/// (`origin/main:src/deluge/gui/menu_item/envelope/segment.h`), which is a `source::PatchedParam` -- a patched param
/// whose index is `p + source_id_` (`origin/main:src/deluge/gui/menu_item/source/patched_param.h`), with the envelope
/// number playing the part of the source id. Main's menu therefore uses `LOCAL_ENV_0_ATTACK` with an envelope
/// index of 0..3 and the four attack params come out consecutive (`instruments::classic::param::kEnv1Attack` to
/// `kEnv4Attack`), which is exactly what a `ParamRef` carries directly here.
///
// clang-format off
/// | node | legacy global | param |
/// |---|---|---|
/// | `env<n>_attack` | `env<n>AttackMenu` | `LOCAL_ENV_<n-1>_ATTACK` |
/// | `env<n>_decay` | `env<n>DecayMenu` | `LOCAL_ENV_<n-1>_DECAY` |
/// | `env<n>_sustain` | `env<n>SustainMenu` | `LOCAL_ENV_<n-1>_SUSTAIN` |
/// | `env<n>_release` | `env<n>ReleaseMenu` | `LOCAL_ENV_<n-1>_RELEASE` |
/// | `env<n>_mode` | `env<n>ModeMenu` | -- (`envelope::Mode`) |
/// | `env<n>_curve` | `env<n>CurveMenu` | -- (`envelope::Curve`) |
/// | `env<n>_follow` | `env<n>FollowMenu` | -- (`envelope::Follow`) |
/// | `env<n>` | `env<n>Menu` | -- (`envelope::EnvelopeMenu`, `origin/main:src/deluge/gui/menu_item/envelope/envelope_menu.h`) |
// clang-format on
///
/// `envelope::Mode`, `Curve` and `Follow` are in `origin/main:src/deluge/gui/menu_item/envelope/settings.h`.
///
/// The envelope's mode renames and hides segments (`Segment::strings`, `Segment::isRelevant`): an AHD's decay is its
/// hold and its release its decay, and it has no sustain; an AR's or MPE's sustain is its level, and it has no decay.
/// The names are `env<n>_..._text`, the hiding `param_shows`. The horizontal page keeps the hidden segments' columns
/// (`EnvelopeMenu`'s `FIXED`), and its segments' page is the envelope's graph. Names and titles with a `*` carry the
/// envelope's number as the node's `title_arg` (`FormattedTitle`).

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/param_shows.h"
#include "ui/scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains

#include <span>

namespace deluge::ui::scenes::menu::nodes {

/// @brief One segment of envelope @p index.
///
/// The first of the four envelopes' ids + @p index is `source::PatchedParam::getP()`
/// (`origin/main:src/deluge/gui/menu_item/source/patched_param.h`) written out: the four envelopes' copies of each
/// parameter are consecutive enumerators (`instruments::classic::param::kEnv1Attack` to `kEnv4Attack`).
constexpr Node envelope_segment(uint16_t name, uint16_t title, int32_t first_param, uint8_t index,
                                ParamShows shows = {}) {
	return with_patch_chain(param(name, {deluge::plugin::ParamKind::PATCHED, static_cast<uint8_t>(first_param + index)},
	                              Hint::Knob, shows, title));
}

/// @brief One envelope page. A macro rather than a function because the four `Node`s have to be
///        distinct objects with static storage for their addresses to go in a `children` array --
///        a `constexpr` factory returning an aggregate cannot hand back pointers into itself.
#define DELUGE_ENVELOPE(n, index)                                                                                      \
	ValueText env##n##_decay_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);       \
	ValueText env##n##_sustain_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);     \
	ValueText env##n##_release_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);     \
	inline constexpr Node env##n##_attack = with_title_arg(                                                            \
	    envelope_segment(                                                                                              \
	        sid(deluge::l10n::String::STRING_FOR_ATTACK), sid(deluge::l10n::String::STRING_FOR_ENV_ATTACK_MENU_TITLE), \
	        deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kEnv1Attack), (index)),   \
	    (n));                                                                                                          \
	inline constexpr Node env##n##_decay =                                                                             \
	    with_name_text(with_title_arg(envelope_segment(sid(deluge::l10n::String::STRING_FOR_DECAY),                    \
	                                                   sid(deluge::l10n::String::STRING_FOR_ENV_DECAY_MENU_TITLE),     \
	                                                   deluge::instruments::classic::runtime_index(                    \
	                                                       deluge::instruments::classic::param::kEnv1Decay),           \
	                                                   (index), param_shows::kEnvelopeDecay[(index)]),                 \
	                                  (n)),                                                                            \
	                   env##n##_decay_text);                                                                           \
	inline constexpr Node env##n##_sustain =                                                                           \
	    with_name_text(with_title_arg(envelope_segment(sid(deluge::l10n::String::STRING_FOR_SUSTAIN),                  \
	                                                   sid(deluge::l10n::String::STRING_FOR_ENV_SUSTAIN_MENU_TITLE),   \
	                                                   deluge::instruments::classic::runtime_index(                    \
	                                                       deluge::instruments::classic::param::kEnv1Sustain),         \
	                                                   (index), param_shows::kEnvelopeSustain[(index)]),               \
	                                  (n)),                                                                            \
	                   env##n##_sustain_text);                                                                         \
	inline constexpr Node env##n##_release =                                                                           \
	    with_name_text(with_title_arg(envelope_segment(sid(deluge::l10n::String::STRING_FOR_RELEASE),                  \
	                                                   sid(deluge::l10n::String::STRING_FOR_ENV_RELEASE_MENU_TITLE),   \
	                                                   deluge::instruments::classic::runtime_index(                    \
	                                                       deluge::instruments::classic::param::kEnv1Release),         \
	                                                   (index)),                                                       \
	                                  (n)),                                                                            \
	                   env##n##_release_text);                                                                         \
	inline constexpr Node env##n##_mode = with_affect_entire(                                                          \
	    with_title_arg(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_MODE), Kind::Enum,                 \
	                                           {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,                 \
	                                            .path = "envelope" #n "Settings/mode"}),                               \
	                              sid(deluge::l10n::String::STRING_FOR_ENV_MODE_MENU_TITLE)),                          \
	                   (n)));                                                                                          \
	inline constexpr Node env##n##_curve = with_affect_entire(                                                         \
	    with_title_arg(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_CURVE), Kind::Int,                 \
	                                           {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,                 \
	                                            .path = "envelope" #n "Settings/curve"}),                              \
	                              sid(deluge::l10n::String::STRING_FOR_ENV_CURVE_MENU_TITLE)),                         \
	                   (n)));                                                                                          \
	inline constexpr Node env##n##_follow = with_affect_entire(with_short_options(                                     \
	    with_title_arg(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_FOLLOW), Kind::Enum,               \
	                                           {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,                 \
	                                            .path = "envelope" #n "Settings/follow"}),                             \
	                              sid(deluge::l10n::String::STRING_FOR_ENV_FOLLOW_MENU_TITLE)),                        \
	                   (n))));                                                                                         \
	inline constexpr const Node* kEnv##n##Children[] = {&env##n##_attack,  &env##n##_decay, &env##n##_sustain,         \
	                                                    &env##n##_release, &env##n##_mode,  &env##n##_curve,           \
	                                                    &env##n##_follow};                                             \
	void env##n##_page_draw(deluge::ui::widgets::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused);     \
	inline constexpr Node env##n = with_page_draw(                                                                     \
	    with_fixed_columns(with_title_arg(                                                                             \
	        submenu(sid(deluge::l10n::String::STRING_FOR_ENVELOPE_MENU_TITLE), kEnv##n##Children, Layout::Horizontal), \
	        (n))),                                                                                                     \
	    env##n##_page_draw, 4)

DELUGE_ENVELOPE(1, 0);
DELUGE_ENVELOPE(2, 1);
DELUGE_ENVELOPE(3, 2);
DELUGE_ENVELOPE(4, 3);

#undef DELUGE_ENVELOPE

/// @brief This file's four contributions to `kBatch1Roots`: `env1` .. `env4`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundEnvChildren;

} // namespace deluge::ui::scenes::menu::nodes
