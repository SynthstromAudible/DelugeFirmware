import { writable } from "svelte/store"

// Top-level capability chip model.
export type ShortcutCapability = {
  id: string
  title: string
  order?: number
}

// Second-level capability chip model.
export type ShortcutSubCapability = {
  id: string
  title: string
  capabilityId: string
  order?: number
}

// Third-level capability chip model.
export type ShortcutSubSubCapability = {
  id: string
  title: string
  subCapabilityId: string
  capabilityId: string
  order?: number
}

// Minimal metadata slice consumed from generated shortcut JSON.
type RawShortcutMetadata = {
  capability?: string
  capabilityParentId?: string
  capabilityOrder?: number
  subCapabilityTitle?: string
  subCapabilityOrder?: number
  subSubCapabilityId?: string
  subSubCapabilityTitle?: string
  subSubCapabilityOrder?: number
}

// Normalizes labels for de-duplication keys.
// Returns lowercase, space-normalized label text.
const normalizeLabel = (value: string) =>
  value
    .trim()
    .toLowerCase()
    .replace(/\s+/g, " ")

  // Converts slug-like ids to presentable labels.
  // Returns title-cased words split from slug/underscore separators.
const toTitleCase = (value: string) =>
  value
    .toLowerCase()
    .split(/[\s_-]+/)
    .filter((part) => part.length > 0)
    .map((part) => part[0].toUpperCase() + part.slice(1))
    .join(" ")

  // Mutable in-memory indexes populated once from shortcut metadata.
export const shortcutCapabilities: ShortcutCapability[] = []
export const shortcutSubCapabilities: ShortcutSubCapability[] = []
export const shortcutSubSubCapabilities: ShortcutSubSubCapability[] = []

const canonicalSubCapabilityIdByKey = new Map<string, string>()
const canonicalSubCapabilityIdByAlias = new Map<string, string>()

const canonicalSubSubCapabilityIdByKey = new Map<string, string>()
const canonicalSubSubCapabilityIdByAlias = new Map<string, string>()

const capabilityBySubCapabilityId = new Map<string, string>()
const subCapabilityTitleById = new Map<string, string>()
const subCapabilityBySubSubCapabilityId = new Map<string, string>()
const subSubCapabilityTitleById = new Map<string, string>()
const capabilityTitleById = new Map<string, string>()

let capabilitiesLoadPromise: Promise<void> | null = null

// Utility numeric comparator for stable sort chains.
// Returns a negative/zero/positive comparator result.
function compareNumbers(a: number, b: number) {
  return a - b
}

// Clears all cached arrays/maps before rebuilding indexes.
// Returns void.
function resetCapabilityCaches() {
  shortcutCapabilities.splice(0, shortcutCapabilities.length)
  shortcutSubCapabilities.splice(0, shortcutSubCapabilities.length)
  shortcutSubSubCapabilities.splice(0, shortcutSubSubCapabilities.length)

  canonicalSubCapabilityIdByKey.clear()
  canonicalSubCapabilityIdByAlias.clear()
  canonicalSubSubCapabilityIdByKey.clear()
  canonicalSubSubCapabilityIdByAlias.clear()

  capabilityBySubCapabilityId.clear()
  subCapabilityTitleById.clear()
  subCapabilityBySubSubCapabilityId.clear()
  subSubCapabilityTitleById.clear()
  capabilityTitleById.clear()
}

// Builds hierarchical capability indexes from raw shortcut metadata.
// Returns void after mutating in-memory index arrays/maps.
function buildCapabilityIndex(shortcuts: RawShortcutMetadata[]) {
  resetCapabilityCaches()

  const seenCapabilityIds = new Set<string>()
  const seenSubCapabilityIds = new Set<string>()
  const seenSubSubCapabilityIds = new Set<string>()

  const capabilityInsertionIndexById = new Map<string, number>()
  const capabilityOrderById = new Map<string, number | undefined>()

  const subCapabilityInsertionIndexById = new Map<string, number>()
  const subCapabilityOrderById = new Map<string, number | undefined>()

  const subSubCapabilityInsertionIndexById = new Map<string, number>()
  const subSubCapabilityOrderById = new Map<string, number | undefined>()

  for (const shortcut of shortcuts) {
    // Branch: skip entries missing top-level capability parent.
    if (!shortcut.capabilityParentId) {
      continue
    }

    // Branch: first time we see this capability id.
    if (!seenCapabilityIds.has(shortcut.capabilityParentId)) {
      const capabilityId = shortcut.capabilityParentId
      shortcutCapabilities.push({
        id: capabilityId,
        title: toTitleCase(capabilityId),
        order: shortcut.capabilityOrder,
      })
      seenCapabilityIds.add(capabilityId)
      capabilityInsertionIndexById.set(capabilityId, shortcutCapabilities.length - 1)
      capabilityOrderById.set(capabilityId, shortcut.capabilityOrder)
    // Branch: update missing order when later record includes one.
    } else if (shortcut.capabilityOrder != null) {
      const currentOrder = capabilityOrderById.get(shortcut.capabilityParentId)
      if (currentOrder == null) {
        capabilityOrderById.set(shortcut.capabilityParentId, shortcut.capabilityOrder)
      }
    }

    if (
      shortcut.capability &&
      shortcut.subCapabilityTitle &&
      !seenSubCapabilityIds.has(shortcut.capability)
    ) {
      // Canonicalize duplicate aliases by parent capability + normalized title.
      const subCapabilityTitle = shortcut.subCapabilityTitle.trim()
      const subCapabilityKey = `${shortcut.capabilityParentId}::${normalizeLabel(subCapabilityTitle)}`

      let canonicalSubCapabilityId = canonicalSubCapabilityIdByKey.get(
        subCapabilityKey,
      )

      if (!canonicalSubCapabilityId) {
        canonicalSubCapabilityId = shortcut.capability
        canonicalSubCapabilityIdByKey.set(
          subCapabilityKey,
          canonicalSubCapabilityId,
        )
      }

      canonicalSubCapabilityIdByAlias.set(
        shortcut.capability,
        canonicalSubCapabilityId,
      )

      // Branch: first canonical sub-capability instance.
      if (!seenSubCapabilityIds.has(canonicalSubCapabilityId)) {
        shortcutSubCapabilities.push({
          id: canonicalSubCapabilityId,
          title: subCapabilityTitle,
          capabilityId: shortcut.capabilityParentId,
          order: shortcut.subCapabilityOrder,
        })
        seenSubCapabilityIds.add(canonicalSubCapabilityId)
        subCapabilityInsertionIndexById.set(
          canonicalSubCapabilityId,
          shortcutSubCapabilities.length - 1,
        )
        subCapabilityOrderById.set(
          canonicalSubCapabilityId,
          shortcut.subCapabilityOrder,
        )
      // Branch: backfill missing order on existing canonical id.
      } else if (shortcut.subCapabilityOrder != null) {
        const currentOrder = subCapabilityOrderById.get(canonicalSubCapabilityId)
        if (currentOrder == null) {
          subCapabilityOrderById.set(canonicalSubCapabilityId, shortcut.subCapabilityOrder)
        }
      }
    }

    if (
      shortcut.capability &&
      shortcut.subSubCapabilityId &&
      shortcut.subSubCapabilityTitle &&
      !seenSubSubCapabilityIds.has(shortcut.subSubCapabilityId)
    ) {
      // Canonicalize third-level aliases by canonical parent + normalized title.
      const canonicalSubCapabilityId =
        canonicalSubCapabilityIdByAlias.get(shortcut.capability) ??
        shortcut.capability
      const subSubCapabilityTitle = shortcut.subSubCapabilityTitle.trim()
      const subSubCapabilityKey = `${canonicalSubCapabilityId}::${normalizeLabel(subSubCapabilityTitle)}`

      let canonicalSubSubCapabilityId = canonicalSubSubCapabilityIdByKey.get(
        subSubCapabilityKey,
      )

      if (!canonicalSubSubCapabilityId) {
        canonicalSubSubCapabilityId = shortcut.subSubCapabilityId
        canonicalSubSubCapabilityIdByKey.set(
          subSubCapabilityKey,
          canonicalSubSubCapabilityId,
        )
      }

      canonicalSubSubCapabilityIdByAlias.set(
        shortcut.subSubCapabilityId,
        canonicalSubSubCapabilityId,
      )

      // Branch: first canonical sub-sub-capability instance.
      if (!seenSubSubCapabilityIds.has(canonicalSubSubCapabilityId)) {
        shortcutSubSubCapabilities.push({
          id: canonicalSubSubCapabilityId,
          title: subSubCapabilityTitle,
          subCapabilityId: canonicalSubCapabilityId,
          capabilityId: shortcut.capabilityParentId,
          order: shortcut.subSubCapabilityOrder,
        })
        seenSubSubCapabilityIds.add(canonicalSubSubCapabilityId)
        subSubCapabilityInsertionIndexById.set(
          canonicalSubSubCapabilityId,
          shortcutSubSubCapabilities.length - 1,
        )
        subSubCapabilityOrderById.set(
          canonicalSubSubCapabilityId,
          shortcut.subSubCapabilityOrder,
        )
      // Branch: backfill missing order on existing canonical id.
      } else if (shortcut.subSubCapabilityOrder != null) {
        const currentOrder = subSubCapabilityOrderById.get(canonicalSubSubCapabilityId)
        if (currentOrder == null) {
          subSubCapabilityOrderById.set(
            canonicalSubSubCapabilityId,
            shortcut.subSubCapabilityOrder,
          )
        }
      }
    }
  }

  // Stable sort capabilities by explicit order, then original insertion.
  shortcutCapabilities.sort((a, b) => {
    const aIndex = capabilityInsertionIndexById.get(a.id) ?? Number.MAX_SAFE_INTEGER
    const bIndex = capabilityInsertionIndexById.get(b.id) ?? Number.MAX_SAFE_INTEGER
    const aOrder = capabilityOrderById.get(a.id) ?? aIndex + 1
    const bOrder = capabilityOrderById.get(b.id) ?? bIndex + 1

    const orderDiff = compareNumbers(aOrder, bOrder)
    if (orderDiff !== 0) {
      return orderDiff
    }

    return compareNumbers(aIndex, bIndex)
  })

  const capabilityPositionById = new Map(
    shortcutCapabilities.map((capability, index) => [capability.id, index]),
  )

  // Stable sort sub-capabilities grouped by parent capability position.
  shortcutSubCapabilities.sort((a, b) => {
    const aCapabilityPosition =
      capabilityPositionById.get(a.capabilityId) ?? Number.MAX_SAFE_INTEGER
    const bCapabilityPosition =
      capabilityPositionById.get(b.capabilityId) ?? Number.MAX_SAFE_INTEGER
    const capabilityDiff = compareNumbers(aCapabilityPosition, bCapabilityPosition)
    if (capabilityDiff !== 0) {
      return capabilityDiff
    }

    const aIndex =
      subCapabilityInsertionIndexById.get(a.id) ?? Number.MAX_SAFE_INTEGER
    const bIndex =
      subCapabilityInsertionIndexById.get(b.id) ?? Number.MAX_SAFE_INTEGER
    const aOrder = subCapabilityOrderById.get(a.id) ?? aIndex + 1
    const bOrder = subCapabilityOrderById.get(b.id) ?? bIndex + 1

    const orderDiff = compareNumbers(aOrder, bOrder)
    if (orderDiff !== 0) {
      return orderDiff
    }

    return compareNumbers(aIndex, bIndex)
  })

  const subCapabilityPositionById = new Map(
    shortcutSubCapabilities.map((subCapability, index) => [subCapability.id, index]),
  )

  // Stable sort sub-sub-capabilities grouped by parent sub-capability.
  shortcutSubSubCapabilities.sort((a, b) => {
    const aSubCapabilityPosition =
      subCapabilityPositionById.get(a.subCapabilityId) ?? Number.MAX_SAFE_INTEGER
    const bSubCapabilityPosition =
      subCapabilityPositionById.get(b.subCapabilityId) ?? Number.MAX_SAFE_INTEGER
    const subCapabilityDiff = compareNumbers(
      aSubCapabilityPosition,
      bSubCapabilityPosition,
    )
    if (subCapabilityDiff !== 0) {
      return subCapabilityDiff
    }

    const aIndex =
      subSubCapabilityInsertionIndexById.get(a.id) ?? Number.MAX_SAFE_INTEGER
    const bIndex =
      subSubCapabilityInsertionIndexById.get(b.id) ?? Number.MAX_SAFE_INTEGER
    const aOrder = subSubCapabilityOrderById.get(a.id) ?? aIndex + 1
    const bOrder = subSubCapabilityOrderById.get(b.id) ?? bIndex + 1

    const orderDiff = compareNumbers(aOrder, bOrder)
    if (orderDiff !== 0) {
      return orderDiff
    }

    return compareNumbers(aIndex, bIndex)
  })

  for (const subCapability of shortcutSubCapabilities) {
    capabilityBySubCapabilityId.set(subCapability.id, subCapability.capabilityId)
    subCapabilityTitleById.set(subCapability.id, subCapability.title)
  }

  for (const subSubCapability of shortcutSubSubCapabilities) {
    subCapabilityBySubSubCapabilityId.set(
      subSubCapability.id,
      subSubCapability.subCapabilityId,
    )
    subSubCapabilityTitleById.set(subSubCapability.id, subSubCapability.title)
  }

  for (const capability of shortcutCapabilities) {
    capabilityTitleById.set(capability.id, capability.title)
  }
}

// Ensures capability indexes are loaded once and reused.
export const ensureCapabilitiesLoaded = async () => {
  if (capabilitiesLoadPromise) {
    return capabilitiesLoadPromise
  }

  capabilitiesLoadPromise = (async () => {
    const module = await import("../data/v4.1.0.json")
    buildCapabilityIndex(module.default as RawShortcutMetadata[])
  })()

  return capabilitiesLoadPromise
}

// Resolves alias sub-capability ids to canonical ids.
export const resolveSubCapabilityId = (subCapabilityId: string | undefined) => {
  if (!subCapabilityId) {
    return undefined
  }

  return canonicalSubCapabilityIdByAlias.get(subCapabilityId) ?? subCapabilityId
}

// Resolves alias sub-sub-capability ids to canonical ids.
export const resolveSubSubCapabilityId = (
  subSubCapabilityId: string | undefined,
) => {
  if (!subSubCapabilityId) {
    return undefined
  }

  return (
    canonicalSubSubCapabilityIdByAlias.get(subSubCapabilityId) ??
    subSubCapabilityId
  )
}

// Returns capability id for a given sub-capability id.
export const getCapabilityIdForSubCapability = (
  subCapabilityId: string | undefined,
) => {
  const resolvedSubCapabilityId = resolveSubCapabilityId(subCapabilityId)
  if (!resolvedSubCapabilityId) {
    return undefined
  }

  return capabilityBySubCapabilityId.get(resolvedSubCapabilityId)
}

// Returns display title for a sub-capability id.
export const getSubCapabilityTitle = (subCapabilityId: string | undefined) => {
  const resolvedSubCapabilityId = resolveSubCapabilityId(subCapabilityId)
  if (!resolvedSubCapabilityId) {
    return undefined
  }

  return subCapabilityTitleById.get(resolvedSubCapabilityId)
}

// Returns sub-capability id for a given sub-sub-capability id.
export const getSubCapabilityIdForSubSubCapability = (
  subSubCapabilityId: string | undefined,
) => {
  const resolvedSubSubCapabilityId = resolveSubSubCapabilityId(
    subSubCapabilityId,
  )
  if (!resolvedSubSubCapabilityId) {
    return undefined
  }

  return subCapabilityBySubSubCapabilityId.get(resolvedSubSubCapabilityId)
}

// Returns display title for a sub-sub-capability id.
export const getSubSubCapabilityTitle = (
  subSubCapabilityId: string | undefined,
) => {
  const resolvedSubSubCapabilityId = resolveSubSubCapabilityId(
    subSubCapabilityId,
  )
  if (!resolvedSubSubCapabilityId) {
    return undefined
  }

  return subSubCapabilityTitleById.get(resolvedSubSubCapabilityId)
}

// Returns display title for a capability id.
export const getCapabilityTitle = (capabilityId: string | undefined) => {
  if (!capabilityId) {
    return undefined
  }

  return capabilityTitleById.get(capabilityId)
}

// Selected top-level capability chip id.
export const activeShortcutCapability = writable<string | null>(null)
// Selected second-level capability chip id.
export const activeShortcutSubCapability = writable<string | null>(null)
// Selected third-level capability chip id.
export const activeShortcutSubSubCapability = writable<string | null>(null)
