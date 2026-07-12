<script lang="ts">
  import { onDestroy, onMount } from "svelte";
  import Cross from "../icons/Cross.svelte";
  import {
    hasPendingSearchInput,
    isSearchLoading,
    searchLoadingProgress,
    searchQuery,
  } from "../stores/search_store.js";

  let inputEl: HTMLInputElement;
  let localQuery = "";
  let progressTimer: ReturnType<typeof setInterval> | undefined;

  // UI-derived state for clear button + enter hint.
  $: hasContent = localQuery.length > 0;
  $: showEnterHint = hasContent && localQuery.trim() !== $searchQuery.trim();
  $: hasPendingSearchInput.set(showEnterHint);

  // Defensive helper so we never leak interval timers.
  // Returns void.
  function clearProgressTimer() {
    if (!progressTimer) {
      return;
    }

    clearInterval(progressTimer);
    progressTimer = undefined;
  }

  // Applies local text to global query with a short, bounded loading animation.
  // Returns a Promise that resolves after query and loading UI state are synchronized.
  async function applySearch(force = false) {
    const nextValue = localQuery.trim();

    // Branch: skip redundant updates unless caller forces commit.
    if (!force && nextValue === $searchQuery.trim()) {
      return;
    }

    isSearchLoading.set(true);
    searchLoadingProgress.set(0);
    clearProgressTimer();

    const startTime = performance.now();
    const minimumDurationMs = nextValue.length === 0 ? 0 : 120;

    // Simulate smooth loading progress while expensive derived filters recompute.
    progressTimer = setInterval(() => {
      const elapsedMs = performance.now() - startTime;
      const progress = Math.min(95, Math.round((elapsedMs / minimumDurationMs) * 95));
      searchLoadingProgress.set(progress);
    }, 30);

    // Yield one frame so loading UI can render before filter recomputation.
    await new Promise((resolve) => requestAnimationFrame(() => resolve(undefined)));
    searchQuery.set(nextValue);

    const elapsedMs = performance.now() - startTime;
    if (minimumDurationMs > 0 && elapsedMs < minimumDurationMs) {
      await new Promise((resolve) => setTimeout(resolve, minimumDurationMs - elapsedMs));
    }

    clearProgressTimer();
    searchLoadingProgress.set(100);
    if (minimumDurationMs > 0) {
      await new Promise((resolve) => setTimeout(resolve, 30));
    }
    isSearchLoading.set(false);
    searchLoadingProgress.set(0);
  }

  // Initialize input from store and autofocus on mount.
  onMount(() => {
    localQuery = $searchQuery;
    isSearchLoading.set(false);
    inputEl.focus();
  });

  // Restore global search UI state on component teardown.
  onDestroy(() => {
    clearProgressTimer();
    hasPendingSearchInput.set(false);
    isSearchLoading.set(false);
    searchLoadingProgress.set(0);
  });

  // Clears local + global query and resets loading flags.
  // Returns void.
  function clear() {
    clearProgressTimer();
    localQuery = "";
    searchQuery.set("");
    hasPendingSearchInput.set(false);
    isSearchLoading.set(false);
    searchLoadingProgress.set(0);
    inputEl.blur();
  }

  // Auto-apply when user erases query back to empty.
  // Returns void.
  function handleInput() {
    // Clearing to empty should trigger the same loading flow as Enter apply.
    if (localQuery.trim().length === 0 && $searchQuery.trim().length > 0) {
      applySearch();
    }
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

    // Branch: escape clears, enter commits.
    if (ev.key === "Escape") {
      clear();
    } else if (ev.key === "Enter") {
      applySearch();
      inputEl.blur();
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
    placeholder="Search... (⌨&#xFE0E; F, Enter / Return to apply)"
    class="w-full rounded-full bg-[var(--sl-color-bg)] pl-6 pr-16 py-3 text-[var(--sl-color-text)] outline outline-1 outline-[var(--sl-color-gray-5)] focus:outline-2"
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
