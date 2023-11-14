#pragma once
#include "gui/menu_item/audio_clip/sample_marker_editor.h"
#include "gui/menu_item/dev_var/dev_var.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/mod_fx/feedback.h"
#include "gui/menu_item/mod_fx/offset.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/menu_item/multi_range.h"
#include "gui/menu_item/osc/source/wave_index.h"
#include "gui/menu_item/osc/sync.h"
#include "gui/menu_item/patch_cables.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/runtime_feature/settings.h"
#include "gui/menu_item/sample/end.h"
#include "gui/menu_item/sample/start.h"
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/unpatched_param.h"

extern deluge::gui::menu_item::dev_var::AMenu devVarAMenu;

extern deluge::gui::menu_item::patched_param::IntegerNonFM noiseMenu;
extern deluge::gui::menu_item::osc::Sync oscSyncMenu;
extern deluge::gui::menu_item::osc::source::WaveIndex sourceWaveIndexMenu;

extern deluge::gui::menu_item::sample::Start sampleStartMenu;
extern deluge::gui::menu_item::sample::End sampleEndMenu;
extern deluge::gui::menu_item::audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart;
extern deluge::gui::menu_item::audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd;
extern DrumName drumNameMenu;

extern deluge::gui::menu_item::firmware::Version firmwareVersionMenu;
extern deluge::gui::menu_item::sequence::Direction sequenceDirectionMenu;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuMIDIOrCV;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuAudioClip;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuPerformanceView;
extern deluge::gui::menu_item::Submenu soundEditorRootMenu;
extern deluge::gui::menu_item::Submenu settingsRootMenu;

//external global FX menu's for use with performanceView editing mode
extern deluge::gui::menu_item::UnpatchedParam globalFXLPFFreqMenu;
extern deluge::gui::menu_item::UnpatchedParam globalFXLPFResMenu;
extern deluge::gui::menu_item::UnpatchedParam globalFXHPFFreqMenu;
extern deluge::gui::menu_item::UnpatchedParam globalFXHPFResMenu;
extern deluge::gui::menu_item::UnpatchedParam bassMenu;
extern deluge::gui::menu_item::UnpatchedParam trebleMenu;
extern deluge::gui::menu_item::UnpatchedParam globalFXReverbSendAmountMenu;
extern deluge::gui::menu_item::UnpatchedParam globalFXDelayFeedbackMenu;
extern deluge::gui::menu_item::UnpatchedParam globalFXDelayRateMenu;
extern deluge::gui::menu_item::mod_fx::Offset modFXOffsetMenu;
extern deluge::gui::menu_item::mod_fx::Feedback modFXFeedbackMenu;
extern deluge::gui::menu_item::UnpatchedParam globalFXModFXDepthMenu;
extern deluge::gui::menu_item::UnpatchedParam globalFXModFXRateMenu;
extern deluge::gui::menu_item::UnpatchedParam srrMenu;
extern deluge::gui::menu_item::UnpatchedParam bitcrushMenu;
extern deluge::gui::menu_item::UnpatchedParam songStutterMenu;

namespace deluge::gui::menu_item::runtime_feature {
extern Submenu subMenuAutomation;
}

extern deluge::gui::menu_item::PatchCables patchCablesMenu;

extern MenuItem* midiOrCVParamShortcuts[8];
extern MenuItem* paramShortcutsForSounds[15][8];
extern MenuItem* paramShortcutsForAudioClips[15][8];
extern MenuItem* paramShortcutsForPerformanceView[15][8];

void setOscillatorNumberForTitles(int32_t);
void setModulatorNumberForTitles(int32_t);
void setEnvelopeNumberForTitles(int32_t);
