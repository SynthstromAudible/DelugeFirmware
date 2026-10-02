import { writable } from "svelte/store"

// Shared cross-card preview policy.
// Keep only one hardware preview open at a time.
const MAX_OPEN_PREVIEWS = 1

// Ordered open preview ids (most-recently opened at end).
export const openHardwarePreviewIds = writable<number[]>([])
// Version tick used to force one-shot UI reset reactions in cards.
export const shortcutPreviewResetVersion = writable(0)

export const setHardwarePreviewOpen = (
  previewId: number,
  shouldOpen: boolean,
) => {
  // Returns void via writable.update side-effect.
  openHardwarePreviewIds.update((current) => {
    // Branch: explicit close removes preview id.
    if (!shouldOpen) {
      return current.filter((id) => id !== previewId)
    }

    // Move this preview to the end (most recently opened).
    const withoutPreview = current.filter((id) => id !== previewId)
    const next = [...withoutPreview, previewId]

    // Branch: no truncation needed.
    if (next.length <= MAX_OPEN_PREVIEWS) {
      return next
    }

    // Enforce open-preview limit.
    return next.slice(next.length - MAX_OPEN_PREVIEWS)
  })
}

export const resetShortcutPreviewUi = () => {
  // Reset version lets cards react even when their own ID was not present.
  // Returns void.
  openHardwarePreviewIds.set([])
  shortcutPreviewResetVersion.update((value) => value + 1)
}
