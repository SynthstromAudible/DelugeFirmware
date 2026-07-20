<script lang="ts">
  import { onDestroy, onMount } from "svelte";
  import Cross from "../icons/Cross.svelte";
  import {
    hasPendingSearchInput,
    searchQuery,
  } from "../stores/search_store.js";

  export let autoFocus = true;

  let inputEl: HTMLInputElement;
  let localQuery = "";
  let applyTimer: ReturnType<typeof setTimeout> | undefined;
  // Match the site-wide Starlight/Pagefind search input debounce.
  const SEARCH_APPLY_DEBOUNCE_MS = 300;

  // UI-derived state for clear button.
  $: hasContent = localQuery.length > 0;
  $: hasPendingSearchInput.set(false);

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
    if (["f", "F"].includes(ev.key)) {
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
<div class="relative block">
  <input
    type="search"
    placeholder="Search... (⌨&#xFE0E; F)"
    class="w-full rounded-full bg-[var(--sl-color-bg)] pl-6 pr-16 py-2 text-[var(--sl-color-text)] leading-6 outline outline-1 outline-[var(--sl-color-gray-5)] focus:outline-2"
    bind:value={localQuery}
    bind:this={inputEl}
    on:input={handleInput}
    on:keydown={handleInputKeyDown}
  />

  <div class="search-actions">
    {#if hasContent}
      <button
        on:click={clear}
        class="aspect-square h-8 rounded-full p-2 text-[var(--sl-color-gray-4)] hover:bg-[var(--sl-color-black)]/10"
        aria-label="Clear search"
      >
        <Cross />
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
  input[type="search"]::-webkit-search-cancel-button {
    -webkit-appearance: none;
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

</style>
