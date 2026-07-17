import { Action } from "./actions.js"
import { Control } from "./targets.js"
import {
  isSubstepContainer,
  type Step,
  type StepOrSubstep,
} from "../types/shortcut.js"

type ShortcutActionDescription =
  | {
      prefix: string
    }
  | {
      sentence: string
    }

export const shortcutActionDescriptions: Record<
  Action,
  ShortcutActionDescription
> = {
  [Action.PRESS]: {
    prefix: "press the",
  },
  [Action.HOLD]: {
    prefix: "hold the",
  },
  [Action.TURN]: {
    prefix: "turn the",
  },
  [Action.MENU]: {
    sentence: "select an option from the on-screen menu",
  },
}

export const shortcutControlDescriptions: Record<Control, string> = {
  [Control.NONE]: "control",
  [Control.RECORD]: "Record button",
  [Control.PLAY]: "Play button",
  [Control.LOAD]: "Load (New) button",
  [Control.SHIFT]: "Shift button",
  [Control.BACK]: "Back / Undo (Redo) button",
  [Control.SAVE]: "Save (Delete) button",
  [Control.TAP]: "Tap Tempo (Metronome) button",
  [Control.SYNC]: "Sync-Scaling button",
  [Control.LEARN]: "Learn / Input button",
  [Control.CLIP]: "Clip button",
  [Control.SYNTH]: "Synth button",
  [Control.KIT]: "Kit button",
  [Control.MIDI]: "MIDI button",
  [Control.CV]: "CV button",
  [Control.CROSS]: "Cross-screen button",
  [Control.ENTIRE]: "Affect Entire button",
  [Control.KEY]: "Key button (keyboard icon)",
  [Control.SCALE]: "Scale button",
  [Control.SONG]: "Song button",
  [Control.X]: "Horizontal (Left/Right) encoder",
  [Control.Y]: "Vertical (Up/Down) encoder",
  [Control.SELECT]: "Select (Settings) encoder",
  [Control.TEMPO]: "Tempo (Swing) encoder",
  [Control.GRID]: "Grid pad (lit or unlit)",
  [Control.GRID_UNLIT]: "Unlit grid pad",
  [Control.GRID_LIT]: "Lit grid pad",
  [Control.AUDITION]: "Audition / Section button for row",
  [Control.LAUNCH]: "Mute / Launch button for row",
  [Control.WAVE_START]: "green grid column representing waveform start",
  [Control.WAVE_END]: "red grid column representing waveform end",
  [Control.WAVE_LOOP_START]:
    "blue grid column representing waveform loop start",
  [Control.WAVE_LOOP_END]: "purple grid column representing waveform loop end",
  [Control.PARAMETER]: "parameter encoders",
  [Control.LOWER_PARAM]: "lower parameter encoder",
  [Control.UPPER_PARAM]: "upper parameter encoder",
  [Control.EXTERNAL]: "external MIDI controller",
  [Control.NOTE_PATCH_SOURCE]: "'Note' patch source pad",
  [Control.RANDOM_PATCH_SOURCE]: "'Random' patch source pad",
  [Control.VELOCITY_PATCH_SOURCE]: "'Velocity' patch source pad",
  [Control.NAME]: "'Name' grid pad",
  [Control.QWERTY]: "QWERTY keyboard overlay",
}

// Returns sentence with leading character capitalized.
const capitalizeSentence = (sentence: string) => {
  return sentence.charAt(0).toUpperCase() + sentence.slice(1)
}

// Returns sentence with a trailing period.
const punctuateSentence = (sentence: string) => {
  return sentence.endsWith(".") ? sentence : `${sentence}.`
}

// Returns a readable phrase for one concrete shortcut step.
const describeStep = (step: Step) => {
  const actionDescription = shortcutActionDescriptions[step.action]

  if ("sentence" in actionDescription) {
    return actionDescription.sentence
  }

  if (step.control === Control.EXTERNAL && step.action === Action.PRESS) {
    return "trigger the external MIDI controller"
  }

  return `${actionDescription.prefix} ${shortcutControlDescriptions[step.control]}`
}

// Returns a readable phrase for either single step or substep container.
const describeStepOrSubstep = (step: StepOrSubstep) => {
  if (isSubstepContainer(step)) {
    return step.substeps.map(describeStep).join(", ")
  }

  return describeStep(step)
}

// Returns a full prose description for a full shortcut step sequence.
export const describeShortcutSteps = (steps: StepOrSubstep[]) => {
  return steps
    .map(describeStepOrSubstep)
    .filter(Boolean)
    .map(capitalizeSentence)
    .map(punctuateSentence)
    .join(" ")
}
