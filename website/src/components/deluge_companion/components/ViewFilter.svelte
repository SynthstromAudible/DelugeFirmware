<script lang="ts">
  import { viewsById } from "../data/views.js";
  import { firmwaresById } from "../data/firmware.js";
  import { allViews, activeView } from "../stores/view_store.js";
  import { allFirmwares, activeFirmware } from "../stores/firmware_store.js";
  import {
    activeShortcutSubSubCapability,
    activeShortcutCapability,
    activeShortcutSubCapability,
    getCapabilityTitle,
    getSubCapabilityTitle,
    getSubSubCapabilityTitle,
    shortcutCapabilities,
    shortcutSubCapabilities,
    shortcutSubSubCapabilities,
  } from "../stores/capability_store.js";
  import {
    activeControl,
    shortcutControlGroups,
  } from "../stores/control_store.js";
  import {
    availableControls,
    availableCapabilityIds,
    availableSubCapabilityIds,
    availableSubSubCapabilityIds,
    availableFirmwares,
    availableViews,
  } from "../stores/shortcut_store.js";
  import { searchQuery } from "../stores/search_store.js";
  import FirmwareFilterItem from "./FirmwareFilterItem.svelte";
  import CapabilityFilterItem from "./CapabilityFilterItem.svelte";
  import SubCapabilityFilterItem from "./SubCapabilityFilterItem.svelte";
  import SubSubCapabilityFilterItem from "./SubSubCapabilityFilterItem.svelte";
  import ControlFilterItem from "./ControlFilterItem.svelte";
  import ViewFilterItem from "./ViewFilterItem.svelte";
  import { resetShortcutPreviewUi } from "../stores/preview_store.js";

  let isFirmwareFilterOpen = true;
  let isCapabilityFilterOpen = true;
  let isSubCapabilityFilterOpen = true;
  let isSubSubCapabilityFilterOpen = true;
  let isViewFilterOpen = true;
  let isControlFilterOpen = false;
  let previousFilterSignature: string | null = null;

  // Any change to active search/facet state should close hardware previews
  // and restore shortcut description sections.
  $: {
    const currentFilterSignature = JSON.stringify([
      $searchQuery.trim(),
      $activeFirmware,
      $activeShortcutCapability,
      $activeShortcutSubCapability,
      $activeShortcutSubSubCapability,
      $activeView,
      $activeControl,
    ]);

    if (
      previousFilterSignature !== null &&
      currentFilterSignature !== previousFilterSignature
    ) {
      resetShortcutPreviewUi();
    }

    previousFilterSignature = currentFilterSignature;
  }

  $: visibleSubCapabilities = shortcutSubCapabilities.filter(
    (subCapability) =>
      subCapability.capabilityId === $activeShortcutCapability &&
      $availableSubCapabilityIds.has(subCapability.id),
  );

  $: visibleSubSubCapabilities = shortcutSubSubCapabilities.filter(
    (subSubCapability) =>
      subSubCapability.subCapabilityId === $activeShortcutSubCapability &&
      $availableSubSubCapabilityIds.has(subSubCapability.id),
  );

  const controlTitleById = new Map(
    shortcutControlGroups.flatMap((group) =>
      group.controls.map((control) => [control.id, control.title]),
    ),
  );

  type SelectedFilterChipKey =
    | "search"
    | "firmware"
    | "capability"
    | "sub-capability"
    | "sub-sub-capability"
    | "view"
    | "control";

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
    $activeControl !== null
      ? {
          key: "control",
          label: `Control: ${controlTitleById.get($activeControl) ?? $activeControl}`,
        }
      : null,
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

    activeControl.set(null);
  }

  function clearAllFilters() {
    // Returns void after resetting all active filters/search and preview state.
    // Reset every active filter source and the shared preview UI state.
    searchQuery.set("");
    activeShortcutCapability.set(null);
    activeShortcutSubCapability.set(null);
    activeShortcutSubSubCapability.set(null);
    activeFirmware.set(null);
    activeView.set(null);
    activeControl.set(null);

    resetShortcutPreviewUi();

    window.dispatchEvent(new CustomEvent("deluge-companion-clear-search"));
  }
</script>

<div class="filter-panel my-4">
  <!-- Firmware facet group. -->
  <section>
    <div class="filter-collapsible">
      <button type="button" class="filter-title filter-toggle" on:click={() => (isFirmwareFilterOpen = !isFirmwareFilterOpen)}>
        <span class:open={isFirmwareFilterOpen} class="filter-caret" aria-hidden="true">
          <svg class="filter-caret-icon" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
            <path d="m14.83 11.29-4.24-4.24a1 1 0 1 0-1.42 1.41L12.71 12l-3.54 3.54a1 1 0 0 0 0 1.41 1 1 0 0 0 .71.29 1 1 0 0 0 .71-.29l4.24-4.24a1.002 1.002 0 0 0 0-1.42Z"></path>
          </svg>
        </span>
        Firmware Filter
      </button>
      {#if isFirmwareFilterOpen}
        <div class="filter-content flex flex-wrap items-center gap-2">
          {#each $allFirmwares as firmware}
            {#if $availableFirmwares.has(firmware.id)}
              <FirmwareFilterItem {firmware} />
            {/if}
          {/each}
        </div>
      {/if}
    </div>
  </section>

  <!-- Top-level capability facet group. -->
  <section>
    <div class="filter-collapsible">
      <button type="button" class="filter-title filter-toggle" on:click={() => (isCapabilityFilterOpen = !isCapabilityFilterOpen)}>
        <span class:open={isCapabilityFilterOpen} class="filter-caret" aria-hidden="true">
          <svg class="filter-caret-icon" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
            <path d="m14.83 11.29-4.24-4.24a1 1 0 1 0-1.42 1.41L12.71 12l-3.54 3.54a1 1 0 0 0 0 1.41 1 1 0 0 0 .71.29 1 1 0 0 0 .71-.29l4.24-4.24a1.002 1.002 0 0 0 0-1.42Z"></path>
          </svg>
        </span>
        Capability Filter
        <span class="filter-note">(Select capability to view sub-capabilities)</span>
      </button>
      {#if isCapabilityFilterOpen}
        <div class="filter-content flex flex-wrap items-center gap-2">
          {#each shortcutCapabilities as capability}
            {#if $availableCapabilityIds.has(capability.id)}
              <CapabilityFilterItem {capability} />
            {/if}
          {/each}
        </div>
      {/if}
    </div>
  </section>

  <!-- Conditional sub-capability facet group (depends on selected capability). -->
  {#if $activeShortcutCapability && visibleSubCapabilities.length > 0}
    <section>
      <div class="filter-collapsible">
        <button type="button" class="filter-title filter-toggle" on:click={() => (isSubCapabilityFilterOpen = !isSubCapabilityFilterOpen)}>
          <span class:open={isSubCapabilityFilterOpen} class="filter-caret" aria-hidden="true">
            <svg class="filter-caret-icon" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
              <path d="m14.83 11.29-4.24-4.24a1 1 0 1 0-1.42 1.41L12.71 12l-3.54 3.54a1 1 0 0 0 0 1.41 1 1 0 0 0 .71.29 1 1 0 0 0 .71-.29l4.24-4.24a1.002 1.002 0 0 0 0-1.42Z"></path>
            </svg>
          </span>
          Sub-Capability Filter
          <span class="filter-note">(Select sub-capability to view sub-sub capabilities)</span>
        </button>
        {#if isSubCapabilityFilterOpen}
          <div class="filter-content flex flex-wrap items-center gap-2">
            {#each visibleSubCapabilities as subCapability}
              <SubCapabilityFilterItem {subCapability} />
            {/each}
          </div>
        {/if}
      </div>
    </section>
  {/if}

  <!-- Conditional sub-sub-capability facet group (depends on selected sub-capability). -->
  {#if $activeShortcutSubCapability && visibleSubSubCapabilities.length > 0}
    <section>
      <div class="filter-collapsible">
        <button type="button" class="filter-title filter-toggle" on:click={() => (isSubSubCapabilityFilterOpen = !isSubSubCapabilityFilterOpen)}>
          <span class:open={isSubSubCapabilityFilterOpen} class="filter-caret" aria-hidden="true">
            <svg class="filter-caret-icon" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
              <path d="m14.83 11.29-4.24-4.24a1 1 0 1 0-1.42 1.41L12.71 12l-3.54 3.54a1 1 0 0 0 0 1.41 1 1 0 0 0 .71.29 1 1 0 0 0 .71-.29l4.24-4.24a1.002 1.002 0 0 0 0-1.42Z"></path>
            </svg>
          </span>
          Sub-Sub-Capability Filter
        </button>
        {#if isSubSubCapabilityFilterOpen}
          <div class="filter-content flex flex-wrap items-center gap-2">
            {#each visibleSubSubCapabilities as subSubCapability}
              <SubSubCapabilityFilterItem {subSubCapability} />
            {/each}
          </div>
        {/if}
      </div>
    </section>
  {/if}

  <!-- View facet group. -->
  <section>
    <div class="filter-collapsible">
      <button type="button" class="filter-title filter-toggle" on:click={() => (isViewFilterOpen = !isViewFilterOpen)}>
        <span class:open={isViewFilterOpen} class="filter-caret" aria-hidden="true">
          <svg class="filter-caret-icon" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
            <path d="m14.83 11.29-4.24-4.24a1 1 0 1 0-1.42 1.41L12.71 12l-3.54 3.54a1 1 0 0 0 0 1.41 1 1 0 0 0 .71.29 1 1 0 0 0 .71-.29l4.24-4.24a1.002 1.002 0 0 0 0-1.42Z"></path>
          </svg>
        </span>
        View Filter
      </button>
      {#if isViewFilterOpen}
        <div class="filter-content flex flex-wrap items-center gap-2">
          {#each $allViews as view}
            {#if $availableViews.has(view.id)}
              <ViewFilterItem {view} />
            {/if}
          {/each}
        </div>
      {/if}
    </div>
  </section>

  <!-- Control facet group (organized into semantic subgroups). -->
  <section>
    <div class="filter-collapsible">
      <button type="button" class="filter-title filter-toggle" on:click={() => (isControlFilterOpen = !isControlFilterOpen)}>
        <span class:open={isControlFilterOpen} class="filter-caret" aria-hidden="true">
          <svg class="filter-caret-icon" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
            <path d="m14.83 11.29-4.24-4.24a1 1 0 1 0-1.42 1.41L12.71 12l-3.54 3.54a1 1 0 0 0 0 1.41 1 1 0 0 0 .71.29 1 1 0 0 0 .71-.29l4.24-4.24a1.002 1.002 0 0 0 0-1.42Z"></path>
          </svg>
        </span>
        Control Filter
      </button>
      {#if isControlFilterOpen}
        <div class="filter-content filter-groups">
          {#each shortcutControlGroups as controlGroup}
            <section class="filter-subgroup">
              <h3 class="filter-subtitle">{controlGroup.title}</h3>
              <div class="flex flex-wrap items-center gap-2">
                {#each controlGroup.controls as control}
                  {#if $availableControls.has(control.id)}
                    <ControlFilterItem {control} />
                  {/if}
                {/each}
              </div>
            </section>
          {/each}
        </div>
      {/if}
    </div>
  </section>

  <!-- Summary bar of currently active filters plus clear-all action. -->
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

  <!-- Usage hint displayed below active filters. -->
  <aside class="selected-filters-help-tip" data-type="tip" aria-label="Tip">
    <div class="selected-filters-help-header">
      <span class="selected-filters-help-icon" aria-hidden="true">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M9 18h6"></path>
          <path d="M10 22h4"></path>
          <path d="M12 2a7 7 0 0 0-4 12.7c.7.5 1.1 1.2 1.2 2.1h5.6c.1-.9.5-1.6 1.2-2.1A7 7 0 0 0 12 2z"></path>
        </svg>
      </span>
      <span class="selected-filters-help-title">Tip</span>
    </div>
    <p class="selected-filters-help-text">
      Click on a shortcut to show the controls on the Deluge interface.
    </p>
  </aside>
</div>

<style>
  .filter-panel {
    --dc-capability-bg: rgb(83 98 120 / 0.3);
    --dc-capability-border: rgb(178 205 222 / 0.72);
    --dc-capability-fg: var(--sl-color-gray-1);

    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  :global(html[data-theme="light"]) .filter-panel {
    --dc-capability-bg: rgb(226 232 240 / 0.9);
    --dc-capability-border: rgb(106 148 187 / 0.85);
    --dc-capability-fg: rgb(41 50 66);
  }

  .filter-title {
    margin: 0;
    font-size: 1.15rem;
    font-weight: 700;
    line-height: 1.2;
    display: flex;
    align-items: center;
  }

  .filter-collapsible > .filter-title {
    cursor: pointer;
    gap: 0.5rem;
  }

  .filter-toggle {
    width: 100%;
    border: 0;
    padding: 0;
    text-align: left;
    color: inherit;
    background: transparent;
  }

  .filter-caret {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 1.25rem;
    height: 1.25rem;
    color: currentColor;
    flex-shrink: 0;
    transition: transform 0.2s ease-in-out;
  }

  .filter-caret-icon {
    width: 1em;
    height: 1em;
  }

  .filter-caret.open {
    transform: rotateZ(90deg);
  }

  .filter-toggle:focus-visible {
    outline: 2px solid var(--dc-capability-border);
    outline-offset: 2px;
    border-radius: 0.25rem;
  }

  .filter-content {
    margin-top: 0.75rem;
  }

  .filter-groups {
    display: flex;
    flex-direction: column;
    gap: 0.875rem;
  }

  .filter-subgroup {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .filter-subtitle {
    margin: 0;
    font-size: 0.95rem;
    font-weight: 600;
    line-height: 1.2;
    color: var(--sl-color-gray-2);
  }

  .filter-note {
    font-size: 0.82rem;
    font-weight: 500;
    color: var(--sl-color-gray-3);
    margin-left: 0.35rem;
    position: relative;
    top: 0.08rem;
  }

  .selected-filters-bar {
    border: 1px solid var(--dc-capability-border);
    border-radius: 0.5rem;
    padding: 0.55rem 0.65rem;
    background: color-mix(in srgb, var(--dc-capability-bg) 45%, transparent);
  }

  .selected-filters-title {
    margin: 0;
    font-size: 0.85rem;
    font-weight: 700;
    line-height: 1.2;
    flex-shrink: 0;
  }

  .clear-filters-btn {
    border: 1px solid var(--dc-capability-border);
    border-radius: 0.35rem;
    background: transparent;
    color: var(--dc-capability-fg);
    font-size: 0.82rem;
    font-weight: 600;
    padding: 0.28rem 0.55rem;
    line-height: 1.2;
    margin-left: auto;
    flex-shrink: 0;
  }

  .clear-filters-btn:hover:not(:disabled) {
    background: color-mix(in srgb, var(--dc-capability-border) 20%, transparent);
    box-shadow: 0 0 0 1px color-mix(in srgb, var(--dc-capability-border) 35%, transparent) inset;
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
    border: 1px solid var(--dc-capability-border);
    border-radius: 999px;
    font-size: 0.78rem;
    line-height: 1.1;
    font-weight: 600;
    color: var(--dc-capability-fg);
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
    background: color-mix(in srgb, var(--dc-capability-border) 18%, transparent);
  }

  .selected-filters-empty {
    font-size: 0.82rem;
    color: var(--sl-color-gray-3);
  }

  .selected-filters-help-tip {
    margin: -0.45rem 0 0;
    border: 1px solid rgb(130 62 176 / 0.88);
    border-inline-start: 0.3rem solid rgb(184 117 255 / 0.95);
    border-radius: 0.5rem;
    padding: 0.78rem 0.9rem;
    font-size: 0.82rem;
    color: rgb(244 234 255);
    background: linear-gradient(180deg, rgb(73 37 95 / 0.96), rgb(64 31 84 / 0.96));
    box-shadow: 0 2px 10px rgb(22 9 30 / 0.28) inset;
  }

  :global(html[data-theme="light"]) .selected-filters-help-tip {
    color: rgb(68 24 97);
    border: 1px solid rgb(181 125 240 / 0.8);
    border-inline-start: 0.3rem solid rgb(158 82 237 / 0.92);
    background: linear-gradient(180deg, rgb(246 234 255 / 0.92), rgb(238 220 255 / 0.92));
    box-shadow: none;
  }

  .selected-filters-help-header {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }

  .selected-filters-help-title {
    font-size: 0.86rem;
    font-weight: 700;
    letter-spacing: 0.01em;
  }

  .selected-filters-help-icon {
    width: 1.15rem;
    height: 1.15rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    opacity: 0.95;
  }

  .selected-filters-help-icon svg {
    width: 100%;
    height: 100%;
  }

  .selected-filters-help-text {
    margin: 0.62rem 0 0;
    font-size: 0.88rem;
    line-height: 1.35;
  }
</style>
