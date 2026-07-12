import { derived, writable } from "svelte/store"
import jsonData from "../data/v4.1.0.json"
import fuzzysort from "fuzzysort"
import { searchQuery } from "./search_store"
import { activeView } from "./view_store"
import { activeShortcutGroup } from "./group_store"
import type {
  Shortcut,
  StepOrSubstep,
  SubstepContainer,
} from "../types/shortcut"

type RawStep =
  | { action: number; control: number }
  | { substeps: { action: number; control: number }[] }

type RawShortcut = Omit<Shortcut, "steps"> & { steps: RawStep[] }

const convertedRawShortcuts: Shortcut[] = (jsonData as RawShortcut[]).map(
  (shortcut) => {
    const steps: StepOrSubstep[] = shortcut.steps.map((step) => {
      if ("substeps" in step) {
        const convertedSubstepContainer: SubstepContainer = {
          substeps: step.substeps,
        }
        return convertedSubstepContainer
      }
      return step
    })

    return { ...shortcut, steps }
  },
)

const rawShortcuts = writable(convertedRawShortcuts)

const allShortcuts = derived(rawShortcuts, ($rawShortcuts) => {
  return $rawShortcuts.map((shortcut) => ({
    ...shortcut,
    fuzzysortPrepared: fuzzysort.prepare(`${shortcut.name}}`),
  }))
})

const filteredByGroups = derived(
  [allShortcuts, activeShortcutGroup],
  ([$shortcuts, $activeShortcutGroup]) => {
    if ($activeShortcutGroup === null) {
      return $shortcuts
    }

    return $shortcuts.filter(
      (shortcut) => shortcut.group === $activeShortcutGroup,
    )
  },
)

const filteredByViews = derived(
  [filteredByGroups, activeView],
  ([$shortcuts, $activeView]) => {
    if ($activeView === null) {
      return $shortcuts
    }
    return $shortcuts.filter((shortcut) => shortcut.views.includes($activeView))
  },
)

export const filteredShortcuts = derived(
  [filteredByViews, searchQuery],
  ([$shortcuts, $searchQuery]) => {
    return fuzzysort
      .go($searchQuery, $shortcuts, {
        key: "fuzzysortPrepared",
        threshold: -1000,
        all: true,
      })
      .map((result) => result.obj)
  },
)
