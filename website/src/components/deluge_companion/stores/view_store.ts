import { writable } from "svelte/store"
import { Views, viewsById } from "../data/views.js"
import type { View } from "../types/shortcut.js"

// Full view list rendered in the view chip row.
export const allViews = writable<View[]>(Object.values(viewsById))

// Active view filter id (or null when unset).
export const activeView = writable<Views | null>(null)
