# Deluge Companion: Developer Guide

This folder contains the interactive Deluge Companion app that is embedded in the docs site.

## High-level architecture

- Entry point: `main.ts`
- Root app shell: `App.svelte`
- Main user view: `views/ShortcutsView.svelte`
- Interactive cards and hardware preview UI: `components/`
- Stateful filtering/search logic: `stores/`
- Generated shortcut dataset: `data/v4.1.0.json`

## Runtime flow

1. `main.ts` mounts `App.svelte` into `#app`.
2. `App.svelte` preloads shortcut/capability data via `ensureShortcutsLoaded()`.
3. After initial render, `App.svelte` dispatches `deluge-companion-ready` for external probes.
4. `ShortcutsView.svelte` renders the page shell (`PageHeader`, `PageMain`, `PageFooter`).
5. `PageMain` subscribes to store-derived results and renders cards through `ShortcutList`.

## Data and filtering model

- Data source is generated markdown -> JSON (`data/v4.1.0.json`).
- `shortcut_store.ts` builds an in-memory index once and exposes derived stores:
  - `filteredShortcuts`
  - `availableCapabilityIds`
  - `availableSubCapabilityIds`
  - `availableSubSubCapabilityIds`
  - `availableViews`
  - `availableFirmwares`
  - `availableControls`
- Search behavior:
  - Uses `fuzzysort` over shortcut name.
  - Prefers strict substring-in-title matches when available.
  - Single-character queries return no results for responsiveness.
- Facet behavior:
  - Filters are AND-combined across active facet stores.
  - Availability sets are recalculated from the currently valid result-space.

## Hardware preview behavior

- `preview_store.ts` coordinates cross-card UI state.
- Only one hardware preview can remain open at a time.
- Opening a preview closes others.
- Clearing filters/search resets preview UI state.
- `Shortcut.svelte` listens to shared preview state and restores description sections when its preview is closed.

## Key conventions

- Keep heavy data loading lazy where possible (`ensureShortcutsLoaded`).
- Prefer derived stores over component-local re-filtering.
- Keep preview coordination in stores, not in sibling component coupling.
- Preserve deterministic ordering by avoiding non-stable keying in list rendering.

## Where to start for common changes

- Search/ranking updates: `stores/shortcut_store.ts`
- Filter interaction updates: `components/ViewFilter.svelte`
- Card interaction/preview updates: `components/Shortcut.svelte`
- Capability taxonomy updates: `stores/capability_store.ts`
- Data content updates: `data/shortcuts/**/*.md` then regenerate JSON
