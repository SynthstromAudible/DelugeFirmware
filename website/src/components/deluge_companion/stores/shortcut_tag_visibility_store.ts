import { writable } from "svelte/store"

// Shared command that can expand/collapse header tags across all cards.
// Default is collapsed to keep card headers compact.
export const shortcutTagVisibilityCommand = writable({
  visible: false,
  version: 0,
})

export const setAllShortcutTagsVisible = (visible: boolean) => {
  shortcutTagVisibilityCommand.update((command) => ({
    visible,
    version: command.version + 1,
  }))
}
