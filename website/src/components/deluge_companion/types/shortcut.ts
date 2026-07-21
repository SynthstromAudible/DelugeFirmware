import { ControlType, Control } from "../data/targets.js"
import type { Views } from "../data/views.js"
import type { Firmwares } from "../data/firmware.js"
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

export type Firmware = {
  id: Firmwares
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
  capability?: string
  capabilityParentId?: string
  capabilityOrder?: number
  subCapabilityTitle?: string
  subCapabilityOrder?: number
  subSubCapabilityId?: string
  subSubCapabilityTitle?: string
  subSubCapabilityOrder?: number
  firmware: Firmwares[]
  views: Views[]
  steps: StepOrSubstep[]
  description?: string
  paragraphs: Paragraph[]
  fuzzysortPrepared?: ReturnType<typeof import("fuzzysort").prepare>
}

export type Paragraph = {
  type?: "community"
  spans: Span[]
}

export type Span = {
  steps?: StepOrSubstep[]
  text?: string
}

// Returns true when candidate is a concrete Step action/control record.
export function isStep(candidate: StepOrSubstep): candidate is Step {
  return "action" in candidate && "control" in candidate
}

// Returns true when candidate is a substep container wrapper.
export function isSubstepContainer(
  candidate: StepOrSubstep,
): candidate is SubstepContainer {
  return "substeps" in candidate
}
