import { derived, writable } from "svelte/store"
import fuzzysort from "fuzzysort"
import { searchQuery } from "./search_store"
import { isShortcutDataLoading } from "./search_store"
import { activeView } from "./view_store"
import { activeFirmware } from "./firmware_store"
import {
  activeShortcutCapability,
  activeShortcutSubCapability,
  activeShortcutSubSubCapability,
  ensureCapabilitiesLoaded,
  getCapabilityIdForSubCapability,
  getSubCapabilityIdForSubSubCapability,
  resolveSubCapabilityId,
  resolveSubSubCapabilityId,
} from "./capability_store"
import { activeControl } from "./control_store"
import { Firmwares } from "../data/firmware"
import { Control } from "../data/targets"
import { Views } from "../data/views"
import type {
  Shortcut,
  StepOrSubstep,
  SubstepContainer,
} from "../types/shortcut"

// Central data pipeline for the companion:
// 1) load/normalize shortcuts
// 2) precompute searchable/filterable index
// 3) derive searched + faceted result sets

// Raw JSON steps can be single actions or substep containers.
type RawStep =
  | { action: number; control: number }
  | { substeps: { action: number; control: number }[] }

// Raw shortcut shape from generated JSON before runtime normalization.
type RawShortcut = Omit<Shortcut, "steps" | "firmware"> & {
  steps: RawStep[]
  firmware?: number[]
}

// Runtime shortcut shape with precomputed search + control metadata.
type PreparedShortcut = Shortcut & {
  fuzzysortPrepared: ReturnType<typeof fuzzysort.prepare>
  normalizedName: string
  controls: ReadonlySet<Control>
}

// Indexed representation reused across derived stores.
type ShortcutIndex = {
  shortcuts: PreparedShortcut[]
  allCapabilityIds: Set<string>
  allSubCapabilityIds: Set<string>
  allSubSubCapabilityIds: Set<string>
  allViews: Set<Views>
  allFirmwares: Set<Firmwares>
  allControls: Set<Control>
}

// Final derived state consumed by filter UI + shortcut list.
type FilterSnapshot = {
  filtered: PreparedShortcut[]
  availableCapabilityIds: Set<string>
  availableSubCapabilityIds: Set<string>
  availableSubSubCapabilityIds: Set<string>
  availableViews: Set<Views>
  availableFirmwares: Set<Firmwares>
  availableControls: Set<Control>
}

let loadShortcutsPromise: Promise<void> | null = null

// Converts generated JSON format into strongly normalized runtime shortcuts.
// Returns normalized shortcut records with canonical capability ids and firmware defaults.
const convertRawShortcuts = (shortcuts: RawShortcut[]): Shortcut[] => {
  return shortcuts.map((shortcut) => {
    // Normalize mixed step/substep records into typed runtime structures.
    const steps: StepOrSubstep[] = shortcut.steps.map((step) => {
      // Branch: this step contains nested substeps.
      if ("substeps" in step) {
        const convertedSubstepContainer: SubstepContainer = {
          substeps: step.substeps,
        }
        return convertedSubstepContainer
      }

      // Branch: this step is a single action/control pair.
      return step
    })

    return {
      ...shortcut,
      // Canonicalize capability ids so aliases map to stable keys.
      capability: resolveSubCapabilityId(shortcut.capability),
      steps,
      subSubCapabilityId: resolveSubSubCapabilityId(
        shortcut.subSubCapabilityId,
      ),
      // If firmware is missing, default shortcut visibility to both tracks.
      firmware:
        shortcut.firmware && shortcut.firmware.length > 0
          ? shortcut.firmware
          : [Firmwares.OFFICIAL, Firmwares.COMMUNITY],
    }
  })
}

export const ensureShortcutsLoaded = async () => {
  // Branch: already loading/loaded -> reuse single promise.
  if (loadShortcutsPromise) {
    // Prevent duplicate concurrent loads and preserve a single ready promise.
    return loadShortcutsPromise
  }

  loadShortcutsPromise = (async () => {
    isShortcutDataLoading.set(true)

    try {
      // Capability maps must exist before shortcut normalization.
      await ensureCapabilitiesLoaded()
      const module = await import("../data/v4.1.0.json")
      const loadedShortcuts = convertRawShortcuts(
        module.default as RawShortcut[],
      )
      rawShortcuts.set(loadedShortcuts)
    } finally {
      // Always clear loading indicator, success or failure.
      isShortcutDataLoading.set(false)
    }
  })()

  return loadShortcutsPromise
}

const rawShortcuts = writable<Shortcut[]>([])

// Collects all control ids referenced by a step tree.
// Returns via mutation of the provided Set; no value is returned.
const collectControlsFromStep = (step: StepOrSubstep, ids: Set<Control>) => {
  // Branch: recurse nested substeps.
  if ("substeps" in step) {
    for (const substep of step.substeps) {
      collectControlsFromStep(substep, ids)
    }
    return
  }

  // Branch: direct step control.
  ids.add(step.control)
}

// Computes unique control ids used by one shortcut.
// Returns a deduplicated set of control ids referenced by the shortcut.
const getShortcutControls = (shortcut: Shortcut): ReadonlySet<Control> => {
  const ids = new Set<Control>()
  for (const step of shortcut.steps) {
    collectControlsFromStep(step, ids)
  }
  return ids
}

const shortcutIndex = derived(rawShortcuts, ($rawShortcuts): ShortcutIndex => {
  // Precompute data used repeatedly by search and facet filtering.
  const shortcuts: PreparedShortcut[] = $rawShortcuts.map((shortcut) => ({
    ...shortcut,
    fuzzysortPrepared: fuzzysort.prepare(shortcut.name),
    normalizedName: shortcut.name.toLowerCase(),
    controls: getShortcutControls(shortcut),
  }))

  const allCapabilityIds = new Set<string>()
  const allSubCapabilityIds = new Set<string>()
  const allSubSubCapabilityIds = new Set<string>()
  const allViews = new Set<Views>()
  const allFirmwares = new Set<Firmwares>()
  const allControls = new Set<Control>()

  for (const shortcut of shortcuts) {
    // Branch: sub-capability is present.
    if (shortcut.capability != null) {
      allSubCapabilityIds.add(shortcut.capability)
      const capabilityId =
        shortcut.capabilityParentId ??
        getCapabilityIdForSubCapability(shortcut.capability)

      // Branch: parent capability could be resolved.
      if (capabilityId) {
        allCapabilityIds.add(capabilityId)
      }
    }

    // Branch: parent capability explicitly present on the record.
    if (shortcut.capabilityParentId != null) {
      allCapabilityIds.add(shortcut.capabilityParentId)
    }

    // Branch: sub-sub-capability present.
    if (shortcut.subSubCapabilityId != null) {
      allSubSubCapabilityIds.add(shortcut.subSubCapabilityId)
    }

    for (const view of shortcut.views) {
      allViews.add(view)
    }

    for (const firmware of shortcut.firmware) {
      allFirmwares.add(firmware)
    }

    for (const control of shortcut.controls) {
      allControls.add(control)
    }
  }

  return {
    shortcuts,
    allCapabilityIds,
    allSubCapabilityIds,
    allSubSubCapabilityIds,
    allViews,
    allFirmwares,
    allControls,
  }
})

const filteredBySearch = derived(
  [shortcutIndex, searchQuery],
  ([$shortcutIndex, $searchQuery]) => {
    const query = $searchQuery.trim()
    const normalizedQuery = query.toLowerCase()

    // Branch: empty query -> no ranking, return indexed order.
    if (query.length === 0) {
      return $shortcutIndex.shortcuts
    }

    // Single-character query is low-signal and expensive to render at scale.
    // Returning no results here keeps backspace and typing responsive.
    // Branch: one-char query -> intentionally hide results.
    if (query.length === 1) {
      return []
    }

    const fuzzyResults = fuzzysort.go(query, $shortcutIndex.shortcuts, {
      key: "fuzzysortPrepared",
      threshold: -1000,
    })

    const strictResults = fuzzyResults.filter((result) =>
      result.obj.normalizedName.includes(normalizedQuery),
    )

    // Prefer strict title-substring matches when available, otherwise use fuzzy ordering.
    // Branch: strict match set exists.
    // Branch: fallback to fuzzy set when strict is empty.
    const preferredResults =
      strictResults.length > 0 ? strictResults : fuzzyResults

    return preferredResults.map((result) => result.obj)
  },
)

const filterState = derived(
  [
    shortcutIndex,
    filteredBySearch,
    searchQuery,
    activeShortcutCapability,
    activeShortcutSubCapability,
    activeShortcutSubSubCapability,
    activeFirmware,
    activeView,
    activeControl,
  ],
  ([
    $shortcutIndex,
    $searchedShortcuts,
    $searchQuery,
    $activeShortcutCapability,
    $activeShortcutSubCapability,
    $activeShortcutSubSubCapability,
    $activeFirmware,
    $activeView,
    $activeControl,
  ]): FilterSnapshot => {
    // Facet filters are applied as AND conditions.
    // In parallel we also compute availability sets for each facet group.
    const hasCapability = $activeShortcutCapability !== null
    const hasSubCapability = $activeShortcutSubCapability !== null
    const hasSubSubCapability = $activeShortcutSubSubCapability !== null
    const hasFirmware = $activeFirmware !== null
    const hasView = $activeView !== null
    const hasControl = $activeControl !== null
    const hasAnyFacet =
      hasCapability ||
      hasSubCapability ||
      hasSubSubCapability ||
      hasFirmware ||
      hasView ||
      hasControl
    const hasSearch = $searchQuery.trim().length > 0

    // Branch: no active search and no facets -> return full index + full availability.
    if (!hasAnyFacet && !hasSearch) {
      return {
        filtered: $shortcutIndex.shortcuts,
        availableCapabilityIds: new Set($shortcutIndex.allCapabilityIds),
        availableSubCapabilityIds: new Set($shortcutIndex.allSubCapabilityIds),
        availableSubSubCapabilityIds: new Set(
          $shortcutIndex.allSubSubCapabilityIds,
        ),
        availableViews: new Set($shortcutIndex.allViews),
        availableFirmwares: new Set($shortcutIndex.allFirmwares),
        availableControls: new Set($shortcutIndex.allControls),
      }
    }

    const availableCapabilityIds = new Set<string>()
    const availableSubCapabilityIds = new Set<string>()
    const availableSubSubCapabilityIds = new Set<string>()
    const availableViews = new Set<Views>()
    const availableFirmwares = new Set<Firmwares>()
    const availableControls = new Set<Control>()
    const filtered: PreparedShortcut[] = []

    for (const shortcut of $searchedShortcuts) {
      const shortcutSubCapabilityId = shortcut.capability
      const shortcutCapabilityId =
        shortcut.capabilityParentId ??
        getCapabilityIdForSubCapability(shortcutSubCapabilityId)
      const shortcutSubSubCapabilityId = shortcut.subSubCapabilityId
      const subCapabilityIdForSubSub = getSubCapabilityIdForSubSubCapability(
        shortcutSubSubCapabilityId,
      )
      const matchesCapability =
        !hasCapability || shortcutCapabilityId === $activeShortcutCapability
      const matchesSubCapability =
        !hasSubCapability ||
        shortcutSubCapabilityId === $activeShortcutSubCapability
      const matchesSubSubCapability =
        !hasSubSubCapability ||
        shortcutSubSubCapabilityId === $activeShortcutSubSubCapability
      const matchesFirmware =
        !hasFirmware || shortcut.firmware.includes($activeFirmware)
      const matchesView = !hasView || shortcut.views.includes($activeView)
      const matchesControl =
        !hasControl || shortcut.controls.has($activeControl)

      // Availability for capability chips under current partial constraints.
      if (
        shortcutCapabilityId != null &&
        matchesFirmware &&
        matchesView &&
        matchesControl &&
        matchesSubCapability &&
        matchesSubSubCapability
      ) {
        availableCapabilityIds.add(shortcutCapabilityId)
      }

      // Availability for sub-capability chips under current partial constraints.
      if (
        shortcutSubCapabilityId != null &&
        matchesFirmware &&
        matchesView &&
        matchesControl &&
        matchesCapability &&
        matchesSubSubCapability
      ) {
        availableSubCapabilityIds.add(shortcutSubCapabilityId)
      }

      // Availability for sub-sub-capability chips under current partial constraints.
      if (
        shortcutSubSubCapabilityId != null &&
        subCapabilityIdForSubSub != null &&
        matchesFirmware &&
        matchesView &&
        matchesControl &&
        matchesCapability &&
        matchesSubCapability
      ) {
        availableSubSubCapabilityIds.add(shortcutSubSubCapabilityId)
      }

      // Availability for view chips.
      if (
        matchesCapability &&
        matchesSubCapability &&
        matchesSubSubCapability &&
        matchesFirmware &&
        matchesControl
      ) {
        for (const view of shortcut.views) {
          availableViews.add(view)
        }
      }

      // Availability for firmware chips.
      if (
        matchesCapability &&
        matchesSubCapability &&
        matchesSubSubCapability &&
        matchesView &&
        matchesControl
      ) {
        for (const firmware of shortcut.firmware) {
          availableFirmwares.add(firmware)
        }
      }

      // Availability for control chips.
      if (
        matchesCapability &&
        matchesSubCapability &&
        matchesSubSubCapability &&
        matchesFirmware &&
        matchesView
      ) {
        for (const control of shortcut.controls) {
          availableControls.add(control)
        }
      }

      // Final AND-filtered result inclusion.
      if (
        matchesCapability &&
        matchesSubCapability &&
        matchesSubSubCapability &&
        matchesFirmware &&
        matchesView &&
        matchesControl
      ) {
        filtered.push(shortcut)
      }
    }

    // Keep active selections visible in filter UI even if current result set is empty.
    if (hasCapability) {
      availableCapabilityIds.add($activeShortcutCapability)
    }

    if (hasSubCapability) {
      availableSubCapabilityIds.add($activeShortcutSubCapability)
    }

    if (hasSubSubCapability) {
      availableSubSubCapabilityIds.add($activeShortcutSubSubCapability)
    }

    if (hasView) {
      availableViews.add($activeView)
    }

    if (hasFirmware) {
      availableFirmwares.add($activeFirmware)
    }

    if (hasControl) {
      availableControls.add($activeControl)
    }

    return {
      filtered,
      availableCapabilityIds,
      availableSubCapabilityIds,
      availableSubSubCapabilityIds,
      availableViews,
      availableFirmwares,
      availableControls,
    }
  },
)

export const availableCapabilityIds = derived(
  filterState,
  ($filterState) => $filterState.availableCapabilityIds,
)

export const availableSubCapabilityIds = derived(
  filterState,
  ($filterState) => $filterState.availableSubCapabilityIds,
)

export const availableSubSubCapabilityIds = derived(
  filterState,
  ($filterState) => $filterState.availableSubSubCapabilityIds,
)

export const availableViews = derived(
  filterState,
  ($filterState) => $filterState.availableViews,
)

export const availableFirmwares = derived(
  filterState,
  ($filterState) => $filterState.availableFirmwares,
)

export const availableControls = derived(
  filterState,
  ($filterState) => $filterState.availableControls,
)

export const filteredShortcuts = derived(
  filterState,
  ($filterState) => $filterState.filtered,
)

void ensureShortcutsLoaded()
