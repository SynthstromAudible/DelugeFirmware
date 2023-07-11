#pragma once
#include "gui/menu_item/dev_var/dev_var.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/multi_range.h"
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/sample/start.h"
#include "gui/menu_item/sample/end.h"
#include "gui/menu_item/audio_clip/sample_marker_editor.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/osc/sync.h"
#include "gui/menu_item/osc/source/wave_index.h"

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
extern deluge::gui::menu_item::Submenu soundEditorRootMenu;
extern deluge::gui::menu_item::Submenu settingsRootMenu;

extern MenuItem* midiOrCVParamShortcuts[8];
extern MenuItem* paramShortcutsForSounds[15][8];
extern MenuItem* paramShortcutsForAudioClips[15][8];

void setOscillatorNumberForTitles(int);
void setModulatorNumberForTitles(int);
void setEnvelopeNumberForTitles(int);
void init_menu_titles();
