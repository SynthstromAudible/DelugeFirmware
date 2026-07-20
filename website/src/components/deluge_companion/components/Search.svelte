<script lang="ts">
  import { onDestroy, onMount } from "svelte";
  import Cross from "../icons/Cross.svelte";
  import {
    hasPendingSearchInput,
    searchQuery,
  } from "../stores/search_store.js";

  export let autoFocus = true;
  export let enableGlobalFocusShortcut = true;
  export let variant: "default" | "toolbar" = "default";

  let inputEl: HTMLInputElement;
  let localQuery = "";
  let applyTimer: ReturnType<typeof setTimeout> | undefined;
  // Match the site-wide Starlight/Pagefind search input debounce.
  const SEARCH_APPLY_DEBOUNCE_MS = 300;

  // UI-derived state for clear button.
  $: hasContent = localQuery.length > 0;
  $: hasPendingSearchInput.set(false);
  $: if (
    inputEl &&
    document.activeElement !== inputEl &&
    localQuery !== $searchQuery
  ) {
    localQuery = $searchQuery;
  }

  // Defensive helper so we never leak delayed apply timers.
  // Returns void.
  function clearApplyTimer() {
    if (!applyTimer) {
      return;
    }

    clearTimeout(applyTimer);
    applyTimer = undefined;
  }

  // Applies local text to the shared query store.
  // Returns void.
  function applySearch(force = false) {
    const nextValue = localQuery.trim();

    // Branch: skip redundant updates unless caller forces commit.
    if (!force && nextValue === $searchQuery.trim()) {
      return;
    }
    searchQuery.set(nextValue);
  }

  // Initialize input from store and autofocus on mount.
  onMount(() => {
    localQuery = $searchQuery;
    if (autoFocus) {
      inputEl.focus();
    }
  });

  // Restore global search UI state on component teardown.
  onDestroy(() => {
    clearApplyTimer();
    hasPendingSearchInput.set(false);
  });

  // Clears local + global query and resets loading flags.
  // Returns void.
  function clear() {
    clearApplyTimer();
    localQuery = "";
    searchQuery.set("");
    hasPendingSearchInput.set(false);
    inputEl.blur();
  }

  // Apply query after a short pause so results don't refresh on every keystroke.
  // Returns void.
  function handleInput() {
    clearApplyTimer();

    const nextValue = localQuery.trim();
    const previousValue = $searchQuery.trim();

    // Fast-path: clearing search should apply immediately.
    if (nextValue.length === 0 && previousValue.length > 0) {
      void applySearch(true);
      return;
    }

    applyTimer = setTimeout(() => {
      void applySearch();
    }, SEARCH_APPLY_DEBOUNCE_MS);
  }

  // Keyboard handling for in-field shortcuts and apply behavior.
  // Returns void.
  function handleInputKeyDown(ev: KeyboardEvent) {
    ev.stopPropagation();

    // Branch: make one-char backspace clear+apply immediately.
    if (ev.key === "Backspace" && localQuery.trim().length === 1) {
      ev.preventDefault();
      localQuery = "";
      applySearch(true);
      return;
    }

    // Branch: escape clears, enter applies immediately.
    if (ev.key === "Escape") {
      clear();
    } else if (ev.key === "Enter") {
      clearApplyTimer();
      void applySearch(true);
    }
  }

  // Global focus shortcut for search input.
  // Returns void.
  function handleGlobalKeyDown(ev: KeyboardEvent) {
    if (enableGlobalFocusShortcut && ["f", "F"].includes(ev.key)) {
      ev.preventDefault();
      inputEl.focus();
    }
  }

  // Handles custom event dispatched by filter UI when clearing all state.
  // Returns void.
  function handleExternalClearSearch() {
    clear();
  }
</script>

<!-- Search input and clear action. -->
<div class:toolbar-search={variant === "toolbar"} class="search-field relative block">
  <input
    type="search"
    placeholder="Search... (⌨&#xFE0E; F)"
    class:toolbar-search-input={variant === "toolbar"}
    class="w-full rounded-full bg-[var(--sl-color-bg)] pl-6 pr-16 py-2 text-[var(--sl-color-text)] leading-6 outline outline-1 outline-[var(--sl-color-gray-5)] focus:outline-none"
    bind:value={localQuery}
    bind:this={inputEl}
    on:input={handleInput}
    on:keydown={handleInputKeyDown}
  />

  <div class="search-actions">
    {#if hasContent}
      <button
        on:click={clear}
        class:toolbar-clear-button={variant === "toolbar"}
        class="search-clear-button"
        aria-label="Clear search"
      >
        {#if variant === "toolbar"}
          Clear
        {:else}
          <Cross />
        {/if}
      </button>
    {/if}
  </div>

</div>

<!-- Global keyboard/event bindings scoped to this component lifecycle. -->
<svelte:window
  on:keydown={handleGlobalKeyDown}
  on:deluge-companion-clear-search={handleExternalClearSearch}
/>

<style lang="postcss">
  input[type="search"] {
    appearance: none;
    -webkit-appearance: none;
  }

  input[type="search"]::-webkit-search-cancel-button {
    -webkit-appearance: none;
  }

  .search-field {
    width: 100%;
  }

  .search-field input[type="search"]:focus {
    outline: none;
    box-shadow: 0 0 0 1px color-mix(in srgb, var(--sl-color-gray-4) 70%, var(--sl-color-gray-5)) inset;
  }

  .search-actions {
    position: absolute;
    right: 0.5rem;
    top: 50%;
    transform: translateY(-50%);
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    min-height: 2rem;
  }

  .search-clear-button {
    aspect-ratio: 1 / 1;
    height: 2rem;
    padding: 0.5rem;
    border-radius: 999px;
    color: var(--sl-color-gray-4);
  }

  .search-clear-button:hover {
    background: color-mix(in srgb, var(--sl-color-black) 10%, transparent);
  }

  .toolbar-search-input {
    min-height: 34px;
    border: 1px solid var(--sl-color-gray-5);
    border-radius: 0.5rem;
    background: var(--sl-color-bg);
    padding: 0.5rem 2.75rem 0.5rem 0.75rem;
    font-size: 0.8125rem;
    font-weight: 400;
    line-height: 1;
    color: inherit;
    outline: 0;
    box-shadow: none;
  }

  .toolbar-search-input:focus {
    outline: none;
  }

  .toolbar-search .search-actions {
    right: 0.5rem;
  }

  .toolbar-search .search-actions .toolbar-clear-button {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    aspect-ratio: auto;
    height: auto;
    min-width: 0;
    width: auto;
    padding: 0.28rem 0.55rem;
    border: 1px solid var(--sl-color-gray-5);
    border-radius: 0.35rem;
    background: transparent;
    color: var(--sl-color-text);
    font-size: 0.82rem;
    font-weight: 600;
    line-height: 1.2;
    box-shadow: none;
  }

  .toolbar-search .search-actions .toolbar-clear-button:hover {
    background: color-mix(in srgb, var(--sl-color-gray-5) 20%, transparent);
    box-shadow: 0 0 0 1px color-mix(in srgb, var(--sl-color-gray-5) 35%, transparent) inset;
  }

  /* iOS Safari auto-zooms focused inputs below 16px. */
  @media (hover: none) and (pointer: coarse) {
    .search-field input[type="search"],
    .search-field .toolbar-search-input {
      font-size: 16px;
    }
  }

</style>
