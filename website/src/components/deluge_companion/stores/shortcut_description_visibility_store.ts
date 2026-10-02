import { writable } from "svelte/store"

// Shared command that can expand/collapse description sections across all cards.
// Default is hidden so users opt in to expanded prose content.
export const shortcutDescriptionVisibilityCommand = writable({
  visible: false,
  version: 0,
})

export const setAllShortcutDescriptionsVisible = (visible: boolean) => {
  shortcutDescriptionVisibilityCommand.update((command) => ({
    visible,
    version: command.version + 1,
  }))
}
