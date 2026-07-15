import type { View } from "../types/shortcut.js"

export enum Views {
  PERFORMANCE,
  ARRANGER,
  SESSION,
  SESSION_ROW,
  SESSION_GRID,
  CLIP,
  CLIP_INSTRUMENT,
  CLIP_AUDIO,
  AUDIO,
  SYNTH,
  KIT,
  MIDI,
  CV,
  WAVEFORM,
  KEYBOARD,
  AUTOMATION,
  AUTOMATION_OVERVIEW,
  AUTOMATION_PARAMETER,
  AUTOMATION_VELOCITY,
  MENU,
  MENU_SETTINGS,
  MENU_SOUND,
  MENU_NOTE,
  MENU_NOTEROW,
}

export type ViewsMap = Record<Views, View>

export const viewsById: ViewsMap = {
  [Views.PERFORMANCE]: {
    id: Views.PERFORMANCE,
    title: "Performance",
    color: "neutral",
  },
  [Views.ARRANGER]: {
    id: Views.ARRANGER,
    title: "Arranger",
    color: "red",
  },
  [Views.SESSION]: {
    id: Views.SESSION,
    title: "Session",
    color: "gold",
  },
  [Views.SESSION_ROW]: {
    id: Views.SESSION_ROW,
    title: "Session (Row Layout)",
    color: "gold",
  },
  [Views.SESSION_GRID]: {
    id: Views.SESSION_GRID,
    title: "Session (Grid Layout)",
    color: "gold",
  },
  [Views.CLIP]: {
    id: Views.CLIP,
    title: "Clip",
    color: "purple",
  },
  [Views.CLIP_INSTRUMENT]: {
    id: Views.CLIP_INSTRUMENT,
    title: "Clip (Instrument)",
    color: "purple",
  },
  [Views.CLIP_AUDIO]: {
    id: Views.CLIP_AUDIO,
    title: "Clip (Audio)",
    color: "purple",
  },
  [Views.AUDIO]: {
    id: Views.AUDIO,
    title: "Audio",
    color: "purple",
  },
  [Views.SYNTH]: {
    id: Views.SYNTH,
    title: "Synth",
    color: "blue",
  },
  [Views.KIT]: {
    id: Views.KIT,
    title: "Kit",
    color: "green",
  },
  [Views.MIDI]: {
    id: Views.MIDI,
    title: "MIDI",
    color: "blue",
  },
  [Views.CV]: {
    id: Views.CV,
    title: "CV",
    color: "blue",
  },
  [Views.WAVEFORM]: {
    id: Views.WAVEFORM,
    title: "Waveform",
    color: "purple",
  },
  [Views.KEYBOARD]: {
    id: Views.KEYBOARD,
    title: "Keyboard",
    color: "purple",
  },
  [Views.AUTOMATION]: {
    id: Views.AUTOMATION,
    title: "Automation",
    color: "purple",
  },
  [Views.AUTOMATION_OVERVIEW]: {
    id: Views.AUTOMATION_OVERVIEW,
    title: "Automation (Sound Parameter Overview)",
    color: "purple",
  },
  [Views.AUTOMATION_PARAMETER]: {
    id: Views.AUTOMATION_PARAMETER,
    title: "Automation (Sound Parameter Editor)",
    color: "purple",
  },
  [Views.AUTOMATION_VELOCITY]: {
    id: Views.AUTOMATION_VELOCITY,
    title: "Automation (Note Velocity Editor)",
    color: "purple",
  },
  [Views.MENU]: {
    id: Views.MENU,
    title: "Menu",
    color: "neutral",
  },
  [Views.MENU_SETTINGS]: {
    id: Views.MENU_SETTINGS,
    title: "Menu (Settings)",
    color: "neutral",
  },
  [Views.MENU_SOUND]: {
    id: Views.MENU_SOUND,
    title: "Menu (Sound Editor)",
    color: "neutral",
  },
  [Views.MENU_NOTE]: {
    id: Views.MENU_NOTE,
    title: "Menu (Note Editor)",
    color: "neutral",
  },
  [Views.MENU_NOTEROW]: {
    id: Views.MENU_NOTEROW,
    title: "Menu (Note Row Editor)",
    color: "neutral",
  },
}
