#pragma once
#include "gui/menu_item/audio_clip/audio_source_selector.h"
#include "gui/menu_item/audio_clip/sample_marker_editor.h"
#include "gui/menu_item/defaults/swing_interval.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/osc/source/wave_index.h"
#include "gui/menu_item/osc/sync.h"
#include "gui/menu_item/patch_cables.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/sample/end.h"
#include "gui/menu_item/sample/start.h"
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/source/patched_param/fm.h"
#include "gui/menu_item/stem_export/start.h"
#include "gui/menu_item/submenu.h"

extern deluge::gui::menu_item::patched_param::IntegerNonFM noiseMenu;
extern deluge::gui::menu_item::osc::Sync oscSyncMenu;
extern deluge::gui::menu_item::osc::source::WaveIndex sourceWaveIndexMenu;

extern deluge::gui::menu_item::sample::Start sampleStartMenu;
extern deluge::gui::menu_item::sample::End sampleEndMenu;
extern deluge::gui::menu_item::audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart;
extern deluge::gui::menu_item::audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd;
extern DrumName drumNameMenu;
extern deluge::gui::menu_item::Submenu dxMenu;
extern deluge::gui::menu_item::Submenu stemExportMenu;
extern deluge::gui::menu_item::stem_export::Start startStemExportMenu;

extern deluge::gui::menu_item::firmware::Version firmwareVersionMenu;
extern deluge::gui::menu_item::sequence::Direction sequenceDirectionMenu;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuMIDIOrCV;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuAudioClip;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuPerformanceView;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuSongView;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuKitGlobalFX;
extern deluge::gui::menu_item::Submenu soundEditorRootMenu;
extern deluge::gui::menu_item::Submenu settingsRootMenu;

// note editor menu's
extern deluge::gui::menu_item::Submenu noteEditorRootMenu;
extern deluge::gui::menu_item::Submenu noteProbabilityMenu;
extern deluge::gui::menu_item::Submenu noteIteranceMenu;
extern deluge::gui::menu_item::Submenu noteFillMenu;
// note row editor menu's
extern deluge::gui::menu_item::Submenu noteRowEditorRootMenu;
extern deluge::gui::menu_item::Submenu noteRowProbabilityMenu;
extern deluge::gui::menu_item::Submenu noteRowIteranceMenu;
extern deluge::gui::menu_item::Submenu noteRowFillMenu;

extern deluge::gui::menu_item::PatchCables patchCablesMenu;
extern deluge::gui::menu_item::source::patched_param::FM modulatorVolume;

extern MenuItem* midiOrCVParamShortcuts[8];
extern MenuItem* paramShortcutsForSounds[15][8];
extern MenuItem* paramShortcutsForAudioClips[15][8];
extern MenuItem* paramShortcutsForSongView[15][8];
extern MenuItem* paramShortcutsForKitGlobalFX[15][8];

void setOscillatorNumberForTitles(int32_t);
void setModulatorNumberForTitles(int32_t);
void setEnvelopeNumberForTitles(int32_t);
