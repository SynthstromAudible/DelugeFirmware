import { writable } from "svelte/store"

// Canonical search query applied to shortcut filtering.
export const searchQuery = writable("")
// True when user has typed text but not committed via Enter.
export const hasPendingSearchInput = writable(false)

// Local loading UI state used by search input/progress hints.
export const isSearchLoading = writable(false)
export const searchLoadingProgress = writable(0)

// Data-loading gate used before shortcut dataset initialization finishes.
export const isShortcutDataLoading = writable(true)
