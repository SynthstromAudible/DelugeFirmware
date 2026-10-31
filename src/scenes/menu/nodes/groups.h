#pragma once

/// @brief The menu groups as data: legacy's `HorizontalMenuGroup`s (`gui/ui/menus.cpp`), each a run of member pages
///        paged as one (`Node::is_group`), and the member pages the groups use that no tree lists.
///
// clang-format off
/// | node | legacy global | members |
/// |---|---|---|
/// | `arp_group` | `arpMenuGroup` (`:397`) | basic, pattern |
/// | `arp_group_kit` | `arpMenuGroupKit` (`:398`) | basic (kit), pattern |
/// | `arp_group_midi` | `arpMenuGroupMIDIOrCV` (`:399`) | basic (MIDI/CV), pattern |
/// | `voice_group` | `voiceMenuGroup` (`:466`) | unison, voice without unison |
/// | `env_group` | `envMenuGroup` (`:469`) | env 1-4 |
/// | `lfo_group` | `lfoMenuGroup` (`:472`) | LFO 1-4 |
/// | `reverb_group` | `reverbMenuGroup` (`:627`) | reverb without sidechain, reverb sidechain |
/// | `filters_group` | `filtersMenuGroup` (`:631`) | LPF, HPF, filter route |
/// | `global_filters_group` | `globalFiltersMenuGroup` (`:752`) | global LPF, global HPF, filter route |
/// | `global_reverb_group` | `globalReverbMenuGroup` (`:819`) | global reverb without sidechain, reverb sidechain |
/// | `source_group` | `sourceMenuGroup` (`:1504`) | osc 1, osc 2, modulator 1, modulator 2, mixer |
// clang-format on

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/roots.h"
#include "scenes/menu/nodes/sound_arp.h"
#include "scenes/menu/nodes/sound_env.h"
#include "scenes/menu/nodes/sound_filters.h"
#include "scenes/menu/nodes/sound_fx.h"
#include "scenes/menu/nodes/sound_lfo.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/menu/nodes/sound_unison.h"
#include "scenes/menu/nodes/sound_voice.h"

#include "gui/l10n/strings.h"

namespace deluge::scenes::menu::nodes {

// --- Member pages no tree lists --------------------------------------------------------------

/// `voiceMenuWithoutUnison` (`menus.cpp:462-465`): the voice page without its unison column, FIXED.
inline constexpr const Node* kVoiceWithoutUnisonChildren[] = {&voice_priority, &polyphony, &voice_count, &portamento};
inline constexpr Node voice_without_unison = with_fixed_columns(
    submenu(sid(deluge::l10n::String::STRING_FOR_VOICE), kVoiceWithoutUnisonChildren, Layout::Horizontal));

/// `reverbMenuWithoutSidechain` (`menus.cpp:622-626`).
inline constexpr const Node* kReverbWithoutSidechainChildren[] = {&reverb_amount, &reverb_room_size, &reverb_damping,
                                                                  &reverb_width,  &reverb_model,     &reverb_pan,
                                                                  &reverb_hpf,    &reverb_lpf};
inline constexpr Node reverb_without_sidechain =
    submenu(sid(deluge::l10n::String::STRING_FOR_REVERB), kReverbWithoutSidechainChildren, Layout::Horizontal);

/// `globalReverbMenuWithoutSidechain` (`menus.cpp:806-818`).
inline constexpr const Node* kGlobalReverbWithoutSidechainChildren[] = {
    &global_reverb_amount, &reverb_room_size, &reverb_damping, &reverb_width,
    &reverb_model,         &reverb_pan,       &reverb_hpf,     &reverb_lpf};
inline constexpr Node global_reverb_without_sidechain =
    submenu(sid(deluge::l10n::String::STRING_FOR_REVERB), kGlobalReverbWithoutSidechainChildren, Layout::Horizontal);

/// `routingHorizontal` (`menus.cpp:630`): the filter route on a page of its own.
inline constexpr const Node* kFilterRoutingPageChildren[] = {&filter_routing};
inline constexpr Node filter_routing_page =
    submenu(sid(deluge::l10n::String::STRING_FOR_FILTER_ROUTE), kFilterRoutingPageChildren, Layout::Horizontal);

// --- The groups ------------------------------------------------------------------------------

inline constexpr const Node* kArpGroupMembers[] = {&arp_basic, &arp_pattern};
inline constexpr Node arp_group = group(sid(deluge::l10n::String::STRING_FOR_ARPEGGIATOR), kArpGroupMembers);
inline constexpr const Node* kArpGroupKitMembers[] = {&arp_basic_kit, &arp_pattern};
inline constexpr Node arp_group_kit = group(sid(deluge::l10n::String::STRING_FOR_ARPEGGIATOR), kArpGroupKitMembers);
inline constexpr const Node* kArpGroupMidiMembers[] = {&arp_basic_midi, &arp_pattern};
inline constexpr Node arp_group_midi = group(sid(deluge::l10n::String::STRING_FOR_ARPEGGIATOR), kArpGroupMidiMembers);

inline constexpr const Node* kVoiceGroupMembers[] = {&unison, &voice_without_unison};
inline constexpr Node voice_group = group(sid(deluge::l10n::String::STRING_FOR_VOICE), kVoiceGroupMembers);

inline constexpr const Node* kEnvGroupMembers[] = {&env1, &env2, &env3, &env4};
inline constexpr Node env_group = group(sid(deluge::l10n::String::STRING_FOR_ENVELOPE_MENU_TITLE), kEnvGroupMembers);

inline constexpr const Node* kLfoGroupMembers[] = {&lfo1, &lfo2, &lfo3, &lfo4};
inline constexpr Node lfo_group = group(sid(deluge::l10n::String::STRING_FOR_LFO1), kLfoGroupMembers);

inline constexpr const Node* kReverbGroupMembers[] = {&reverb_without_sidechain, &reverb_sidechain};
inline constexpr Node reverb_group = group(sid(deluge::l10n::String::STRING_FOR_REVERB), kReverbGroupMembers);

inline constexpr const Node* kFiltersGroupMembers[] = {&lpf, &hpf, &filter_routing_page};
inline constexpr Node filters_group = group(sid(deluge::l10n::String::STRING_FOR_FILTERS), kFiltersGroupMembers);

inline constexpr const Node* kGlobalFiltersGroupMembers[] = {&global_lpf, &global_hpf, &filter_routing_page};
inline constexpr Node global_filters_group =
    group(sid(deluge::l10n::String::STRING_FOR_FILTERS), kGlobalFiltersGroupMembers);

inline constexpr const Node* kGlobalReverbGroupMembers[] = {&global_reverb_without_sidechain, &reverb_sidechain};
inline constexpr Node global_reverb_group =
    group(sid(deluge::l10n::String::STRING_FOR_REVERB), kGlobalReverbGroupMembers);

inline constexpr const Node* kSourceGroupMembers[] = {&source0, &source1, &modulator0, &modulator1, &osc_mixer};
inline constexpr Node source_group = group(sid(deluge::l10n::String::STRING_FOR_OSCILLATOR_1), kSourceGroupMembers);

} // namespace deluge::scenes::menu::nodes
