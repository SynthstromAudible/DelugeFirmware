import { ControlType, Control } from "../data/targets.js"
import type { Views } from "../data/views.js"
import type { Action } from "../data/actions.js"

export type ActionDescriptions = Record<
  Action,
  { title: string; classes: string }
>

export type ControlDescriptions = Record<
  Control,
  {
    title: string
    type: ControlType
    color?: string
    classes?: string[]
  }
>

export type View = {
  id: Views
  title: string
  color: string
}

export type Step = {
  action: Action
  control: Control
  label?: string
}

export type SubstepContainer = {
  substeps: Step[]
}

export type StepOrSubstep = Step | SubstepContainer

export type Shortcut = {
  name: string
  views: Views[]
  steps: StepOrSubstep[]
  paragraphs: Paragraph[]
  fuzzysortPrepared?: ReturnType<typeof import("fuzzysort").prepare>
}

export type Paragraph = {
  spans: Span[]
}

export type Span = {
  steps?: StepOrSubstep[]
  text?: string
}

export function isStep(candidate: StepOrSubstep): candidate is Step {
  return "action" in candidate && "control" in candidate
}

export function isSubstepContainer(
  candidate: StepOrSubstep,
): candidate is SubstepContainer {
  return "substeps" in candidate
}
