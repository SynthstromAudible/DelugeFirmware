import { writable } from "svelte/store"

export type ShortcutGroup = {
  id: string
  title: string
}

export const shortcutGroups: ShortcutGroup[] = [
  { id: "00-global", title: "Global" },
  { id: "01-sequencing", title: "Sequencing" },
  { id: "02-song_view", title: "Song View" },
  { id: "03-recording_and_resampling", title: "Recording & Resampling" },
  { id: "04-audio_clips", title: "Audio Clips" },
  { id: "05-modifying_sounds", title: "Modifying Sounds" },
  { id: "06-waveforms", title: "Waveforms" },
  { id: "07-arranger", title: "Arranger" },
  { id: "08-parameter_dials", title: "Parameter Dials" },
  { id: "09-midi_commands", title: "MIDI Commands" },
  { id: "10-looper", title: "Looper" },
  { id: "11-community_features", title: "Community Features" },
]

export const activeShortcutGroup = writable<string | null>(null)
