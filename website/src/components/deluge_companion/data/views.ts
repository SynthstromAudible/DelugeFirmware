import type { View } from "../types/shortcut.js"

export enum Views {
  GLOBAL,
  SONG,
  ARRANGER,
  PERFORMANCE,
  SYNTH,
  KIT,
  MIDI,
  CV,
  AUDIO,
  WAVEFORM,
  CLIP,
  AUTOMATION,
  VELOCITY,
  KEYBOARD,
}

export type ViewsMap = Record<Views, View>

export const viewsById: ViewsMap = {
  [Views.GLOBAL]: {
    id: Views.GLOBAL,
    title: "Global",
    color: "neutral",
  },
  [Views.SONG]: {
    id: Views.SONG,
    title: "Song",
    color: "gold",
  },
  [Views.ARRANGER]: {
    id: Views.ARRANGER,
    title: "Arranger",
    color: "red",
  },
  [Views.PERFORMANCE]: {
    id: Views.PERFORMANCE,
    title: "Performance",
    color: "red",
  },
  [Views.SYNTH]: {
    id: Views.SYNTH,
    title: "Synth",
    color: "green",
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
  [Views.AUDIO]: {
    id: Views.AUDIO,
    title: "Audio",
    color: "purple",
  },
  [Views.WAVEFORM]: {
    id: Views.WAVEFORM,
    title: "Waveform",
    color: "purple",
  },
  [Views.CLIP]: {
    id: Views.CLIP,
    title: "Clip",
    color: "purple",
  },
  [Views.AUTOMATION]: {
    id: Views.AUTOMATION,
    title: "Automation",
    color: "purple",
  },
  [Views.VELOCITY]: {
    id: Views.VELOCITY,
    title: "Velocity",
    color: "purple",
  },
  [Views.KEYBOARD]: {
    id: Views.KEYBOARD,
    title: "Keyboard",
    color: "purple",
  },
}
