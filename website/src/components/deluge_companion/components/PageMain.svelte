<script>
  import ShortcutList from "./ShortcutList.svelte";
  import ViewFilter from "./ViewFilter.svelte";
  import {
    hasPendingSearchInput,
    isSearchLoading,
    isShortcutDataLoading,
    searchLoadingProgress,
  } from "../stores/search_store.js";

  // Main content gate: show loading/status states until search data is ready,
  // then render filters + virtualized list.
</script>

<main class="mb-8 flex flex-col gap-4">
  <!-- Branch: show loading/pending states before full filter/list UI. -->
  {#if $isShortcutDataLoading || $isSearchLoading || $hasPendingSearchInput}
    <div class="loading-shortcuts rounded-md border border-[var(--sl-color-gray-5)] px-4 py-3 text-sm">
      <!-- Branch: initial dataset load. -->
      {#if $isShortcutDataLoading}
        <p class="loading-title">Loading shortcuts dataset...</p>
        <div class="loading-bar" aria-hidden="true">
          <span
            class="loading-bar-indicator loading-bar-indicator-indeterminate"
            style={`width: 36%;`}
          ></span>
        </div>
      <!-- Branch: asynchronous search/filter refresh progress. -->
      {:else if $isSearchLoading}
        <p class="loading-title">Loading shortcuts...</p>
        <div class="loading-bar" aria-hidden="true">
          <span
            class="loading-bar-indicator"
            style={`width: ${Math.max($searchLoadingProgress, 4)}%;`}
          ></span>
        </div>
      <!-- Branch: user has typed but has not submitted search yet. -->
      {:else}
        <p class="loading-title">
          Press Enter / Return to search
        </p>
      {/if}
    </div>
  {:else}
    <!-- Ready state: render filter controls and incremental result list. -->
    <ViewFilter />
    <ShortcutList />
  {/if}
</main>

<style>
  .loading-shortcuts {
    color: var(--sl-color-gray-2);
    background: color-mix(in srgb, var(--sl-color-bg) 78%, var(--sl-color-gray-6));
  }

  .loading-title {
    margin: 0;
  }

  .loading-bar {
    margin-top: 0.6rem;
    height: 0.32rem;
    border-radius: 9999px;
    overflow: hidden;
    background: rgba(148, 163, 184, 0.35);
    background: color-mix(in srgb, var(--sl-color-gray-5) 55%, transparent);
    position: relative;
  }

  .loading-bar-indicator {
    position: absolute;
    inset-block: 0;
    left: 0;
    width: 0;
    border-radius: inherit;
    background: linear-gradient(90deg, #60a5fa 0%, #3b82f6 55%, #2563eb 100%);
    background: color-mix(in srgb, var(--sl-color-accent) 75%, var(--sl-color-white, #fff));
    transition: width 120ms ease-out;
  }

  .loading-bar-indicator-indeterminate {
    animation: shortcut-data-loading 900ms ease-in-out infinite;
    transition: none;
  }

  @keyframes shortcut-data-loading {
    0% {
      transform: translateX(-10%);
    }
    100% {
      transform: translateX(210%);
    }
  }

  :global(html[data-theme="light"]) .loading-shortcuts {
    color: var(--sl-color-gray-3);
    background: color-mix(in srgb, var(--sl-color-bg) 88%, var(--sl-color-gray-6));
  }
</style>
