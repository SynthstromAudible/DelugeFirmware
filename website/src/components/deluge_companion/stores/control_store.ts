import { writable } from "svelte/store"
import { Control } from "../data/targets.js"

// Flat chip model used by control filter components.
export type ShortcutControlFilter = {
  id: Control
  title: string
}

// Group model used to render titled control filter sections.
export type ShortcutControlGroup = {
  title: string
  controls: ShortcutControlFilter[]
}

// Static control groups shown in the filter panel.
export const shortcutControlGroups: ShortcutControlGroup[] = [
  {
    title: "Transport & System",
    controls: [
      { id: Control.RECORD, title: "Record" },
      { id: Control.PLAY, title: "Play" },
      { id: Control.LOAD, title: "Load" },
      { id: Control.SAVE, title: "Save / Delete" },
      { id: Control.BACK, title: "Back / Undo / Redo" },
      { id: Control.SHIFT, title: "Shift" },
      { id: Control.TAP, title: "Tap Tempo" },
      { id: Control.SYNC, title: "Sync-Scaling" },
      { id: Control.LEARN, title: "Learn / Input" },
      { id: Control.ENTIRE, title: "Affect Entire" },
    ],
  },
  {
    title: "Views & Mode Keys",
    controls: [
      { id: Control.SONG, title: "Song" },
      { id: Control.CLIP, title: "Clip" },
      { id: Control.SYNTH, title: "Synth" },
      { id: Control.KIT, title: "Kit" },
      { id: Control.MIDI, title: "MIDI" },
      { id: Control.CV, title: "CV" },
      { id: Control.CROSS, title: "Cross-screen" },
      { id: Control.PERFORMANCE, title: "Performance" },
      { id: Control.KEY, title: "Keyboard" },
      { id: Control.SCALE, title: "Scale" },
      { id: Control.EXTERNAL, title: "External MIDI Controller" },
    ],
  },
  {
    title: "Encoders",
    controls: [
      { id: Control.X, title: "Horizontal Encoder" },
      { id: Control.Y, title: "Vertical Encoder" },
      { id: Control.SELECT, title: "Select Encoder" },
      { id: Control.TEMPO, title: "Tempo Encoder" },
      { id: Control.PARAMETER, title: "Gold Encoders" },
      { id: Control.LOWER_PARAM, title: "Lower Gold Encoder" },
      { id: Control.UPPER_PARAM, title: "Upper Gold Encoder" },
    ],
  },
  {
    title: "Parameter Buttons",
    controls: [
      { id: Control.PARAMBUTTON, title: "PARAM BUTTON" },
      { id: Control.PARAMBUTTON1, title: "LEVEL/PAN BUTTON" },
      { id: Control.PARAMBUTTON2, title: "CUTOFF/RES BUTTON" },
      { id: Control.PARAMBUTTON3, title: "ATTACK/RELEASE BUTTON" },
      { id: Control.PARAMBUTTON4, title: "DELAY TIME/AMOUNT BUTTON" },
      { id: Control.PARAMBUTTON5, title: "SIDECHAIN/REVERB BUTTON" },
      { id: Control.PARAMBUTTON6, title: "MOD RATE/DEPTH BUTTON" },
      { id: Control.PARAMBUTTON7, title: "STUTTER/CUSTOM 1 BUTTON" },
      { id: Control.PARAMBUTTON8, title: "CUSTOM 2/3 BUTTON" },
    ],
  },
  {
    title: "Pads & Rows",
    controls: [
      { id: Control.GRID, title: "Grid Pad" },
      { id: Control.GRID_UNLIT, title: "Unlit Grid Pad" },
      { id: Control.GRID_LIT, title: "Lit Grid Pad" },
      { id: Control.AUDITION, title: "Audition / Section" },
      { id: Control.LAUNCH, title: "Mute / Launch" },
      { id: Control.WAVE_START, title: "Wave Start" },
      { id: Control.WAVE_END, title: "Wave End" },
      { id: Control.WAVE_LOOP_START, title: "Wave Loop Start" },
      { id: Control.WAVE_LOOP_END, title: "Wave Loop End" },
      { id: Control.NOTE_PATCH_SOURCE, title: "Note Patch Source Pad" },
      { id: Control.RANDOM_PATCH_SOURCE, title: "Random Patch Source Pad" },
      { id: Control.VELOCITY_PATCH_SOURCE, title: "Velocity Patch Source Pad" },
      { id: Control.NAME, title: "Name Grid Pad" },
    ],
  },
]

// Currently selected control filters (multi-select).
export const activeControl = writable<Set<Control>>(new Set())
