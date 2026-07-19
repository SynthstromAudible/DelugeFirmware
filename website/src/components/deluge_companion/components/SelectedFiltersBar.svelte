<script lang="ts">
  import { viewsById } from "../data/views.js";
  import { firmwaresById } from "../data/firmware.js";
  import { activeView } from "../stores/view_store.js";
  import { activeFirmware } from "../stores/firmware_store.js";
  import {
    activeShortcutSubSubCapability,
    activeShortcutCapability,
    activeShortcutSubCapability,
    getCapabilityTitle,
    getSubCapabilityTitle,
    getSubSubCapabilityTitle,
  } from "../stores/capability_store.js";
  import {
    activeControl,
    shortcutControlGroups,
  } from "../stores/control_store.js";
  import { searchQuery } from "../stores/search_store.js";
  import { resetShortcutPreviewUi } from "../stores/preview_store.js";

  const controlTitleById = new Map(
    shortcutControlGroups.flatMap((group) =>
      group.controls.map((control) => [control.id, control.title]),
    ),
  );

  type SelectedFilterChipKey = string;

  type SelectedFilterChip = {
    key: SelectedFilterChipKey;
    label: string;
  };

  // Derive a compact chip list from all active filters/search.
  $: selectedFilterChips = [
    $searchQuery.trim().length > 0
      ? {
          key: "search",
          label: `Search: ${$searchQuery.trim()}`,
        }
      : null,
    $activeFirmware !== null
      ? {
          key: "firmware",
          label: `Firmware: ${firmwaresById[$activeFirmware]?.title ?? $activeFirmware}`,
        }
      : null,
    $activeShortcutCapability
      ? {
          key: "capability",
          label: `Capability: ${getCapabilityTitle($activeShortcutCapability) ?? $activeShortcutCapability}`,
        }
      : null,
    $activeShortcutSubCapability
      ? {
          key: "sub-capability",
          label: `Sub-Capability: ${getSubCapabilityTitle($activeShortcutSubCapability) ?? $activeShortcutSubCapability}`,
        }
      : null,
    $activeShortcutSubSubCapability
      ? {
          key: "sub-sub-capability",
          label: `Sub-Sub-Capability: ${getSubSubCapabilityTitle($activeShortcutSubSubCapability) ?? $activeShortcutSubSubCapability}`,
        }
      : null,
    $activeView !== null
      ? {
          key: "view",
          label: `View: ${viewsById[$activeView]?.title ?? $activeView}`,
        }
      : null,
    ...[...$activeControl].map((controlId) => ({
      key: `control:${controlId}`,
      label: `Control: ${controlTitleById.get(controlId) ?? controlId}`,
    })),
  ].filter((chip): chip is SelectedFilterChip => chip !== null);

  function removeSelectedFilter(key: SelectedFilterChipKey) {
    // Returns void after clearing the selected filter source.
    // Search uses a companion-specific clear event so header input stays in sync.
    if (key === "search") {
      searchQuery.set("");
      resetShortcutPreviewUi();
      window.dispatchEvent(new CustomEvent("deluge-companion-clear-search"));
      return;
    }

    if (key === "firmware") {
      activeFirmware.set(null);
      return;
    }

    if (key === "capability") {
      activeShortcutCapability.set(null);
      activeShortcutSubCapability.set(null);
      activeShortcutSubSubCapability.set(null);
      return;
    }

    if (key === "sub-capability") {
      activeShortcutSubCapability.set(null);
      activeShortcutSubSubCapability.set(null);
      return;
    }

    if (key === "sub-sub-capability") {
      activeShortcutSubSubCapability.set(null);
      return;
    }

    if (key === "view") {
      activeView.set(null);
      return;
    }

    if (key.startsWith("control:")) {
      const id = Number(key.slice("control:".length));
      activeControl.update((prev) => {
        const next = new Set(prev);
        next.delete(id);
        return next;
      });
      return;
    }
  }

  function clearAllFilters() {
    // Returns void after resetting all active filters/search and preview state.
    searchQuery.set("");
    activeShortcutCapability.set(null);
    activeShortcutSubCapability.set(null);
    activeShortcutSubSubCapability.set(null);
    activeFirmware.set(null);
    activeView.set(null);
    activeControl.set(new Set());

    resetShortcutPreviewUi();

    window.dispatchEvent(new CustomEvent("deluge-companion-clear-search"));
  }
</script>

<section class="selected-filters-bar" aria-label="Selected filters">
  <div class="selected-filters-content">
    <h3 class="selected-filters-title">Selected Filters</h3>
    {#if selectedFilterChips.length > 0}
      {#each selectedFilterChips as chip (chip.key)}
        <span class="selected-filter-chip">
          <span>{chip.label}</span>
          <button
            type="button"
            class="selected-filter-chip-remove"
            aria-label={`Remove ${chip.label} filter`}
            on:click={() => removeSelectedFilter(chip.key)}
          >
            ×
          </button>
        </span>
      {/each}
    {:else}
      <span class="selected-filters-empty">No filters selected.</span>
    {/if}

    <button
      class="clear-filters-btn"
      on:click={clearAllFilters}
      disabled={selectedFilterChips.length === 0}
    >
      Clear All Filters
    </button>
  </div>
</section>

<style>
  .selected-filters-bar {
    border: 1px solid var(--dc-capability-border, var(--sl-color-gray-5));
    border-radius: 0.5rem;
    padding: 0.55rem 0.65rem;
    background: color-mix(in srgb, var(--dc-capability-bg, var(--sl-color-gray-6)) 45%, transparent);
    margin-bottom: 0.7rem;
  }

  .selected-filters-title {
    margin: 0;
    font-size: 0.85rem;
    font-weight: 700;
    line-height: 1.2;
    flex-shrink: 0;
  }

  .clear-filters-btn {
    border: 1px solid var(--dc-capability-border, var(--sl-color-gray-5));
    border-radius: 0.35rem;
    background: transparent;
    color: var(--dc-capability-fg, var(--sl-color-text));
    font-size: 0.82rem;
    font-weight: 600;
    padding: 0.28rem 0.55rem;
    line-height: 1.2;
    margin-left: auto;
    flex-shrink: 0;
  }

  .clear-filters-btn:hover:not(:disabled) {
    background: color-mix(in srgb, var(--dc-capability-border, var(--sl-color-gray-5)) 20%, transparent);
    box-shadow: 0 0 0 1px color-mix(in srgb, var(--dc-capability-border, var(--sl-color-gray-5)) 35%, transparent) inset;
  }

  .clear-filters-btn:disabled {
    opacity: 0.45;
    cursor: not-allowed;
  }

  .selected-filters-content {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.45rem;
  }

  .selected-filter-chip {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    border: 1px solid var(--dc-capability-border, var(--sl-color-gray-5));
    border-radius: 999px;
    font-size: 0.78rem;
    line-height: 1.1;
    font-weight: 600;
    color: var(--dc-capability-fg, var(--sl-color-text));
    background: transparent;
    padding: 0.23rem 0.55rem;
    flex-shrink: 0;
  }

  .selected-filter-chip-remove {
    border: 0;
    background: transparent;
    color: inherit;
    width: 1rem;
    height: 1rem;
    padding: 0;
    border-radius: 999px;
    line-height: 1;
    font-size: 0.95rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    opacity: 0.82;
  }

  .selected-filter-chip-remove:hover {
    opacity: 1;
    background: color-mix(in srgb, var(--dc-capability-border, var(--sl-color-gray-5)) 18%, transparent);
  }

  .selected-filters-empty {
    font-size: 0.82rem;
    color: var(--sl-color-gray-3);
  }
</style>
