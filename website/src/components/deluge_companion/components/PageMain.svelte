<script>
  import ShortcutList from "./ShortcutList.svelte";
  import ViewFilter from "./ViewFilter.svelte";
  import { isShortcutDataLoading, searchQuery } from "../stores/search_store.js";
  import { filteredShortcuts } from "../stores/shortcut_store.js";
  import { setAllShortcutTagsVisible } from "../stores/shortcut_tag_visibility_store.js";
  import { setAllShortcutDescriptionsVisible } from "../stores/shortcut_description_visibility_store.js";

  // Hide filters when an active search has no matches.
  $: hasNoSearchMatches =
    $searchQuery.trim().length > 0 && $filteredShortcuts.length === 0;

  let areAllTagsVisible = false;
  let areAllDescriptionsVisible = false;

  // Applies global tag visibility command across all currently rendered cards.
  function onTagVisibilityChanged() {
    setAllShortcutTagsVisible(areAllTagsVisible);
  }

  // Applies global description visibility command across all currently rendered cards.
  function onDescriptionVisibilityChanged() {
    setAllShortcutDescriptionsVisible(areAllDescriptionsVisible);
  }
</script>

<main class="mb-8 flex flex-col gap-4">
  <!-- Branch: show loading state before full filter/list UI. -->
  {#if $isShortcutDataLoading}
    <div class="loading-shortcuts rounded-md border border-[var(--sl-color-gray-5)] px-4 py-3 text-sm">
      <p class="loading-title">Loading shortcuts dataset...</p>
      <div class="loading-bar" aria-hidden="true">
        <span
          class="loading-bar-indicator loading-bar-indicator-indeterminate"
          style={`width: 36%;`}
        ></span>
      </div>
    </div>
  {:else}
    <!-- Ready state: hide filters when a search has no matches. -->
    {#if !hasNoSearchMatches}
      <ViewFilter />
    {/if}

    <section class="shortcut-global-toggles" aria-label="Shortcut visibility controls">
      <label class="description-visibility-label" for="toggle-shortcut-tags">
        <input
          id="toggle-shortcut-tags"
          class="description-visibility-input"
          type="checkbox"
          bind:checked={areAllTagsVisible}
          on:change={onTagVisibilityChanged}
        />
        <span class="description-visibility-text">
          {areAllTagsVisible ? "Hide all shortcut tags" : "Show all shortcut tags"}
        </span>
      </label>

      <label class="description-visibility-label" for="toggle-shortcut-descriptions">
        <input
          id="toggle-shortcut-descriptions"
          class="description-visibility-input"
          type="checkbox"
          bind:checked={areAllDescriptionsVisible}
          on:change={onDescriptionVisibilityChanged}
        />
        <span class="description-visibility-text">
          {areAllDescriptionsVisible ? "Hide all shortcut descriptions" : "Show all shortcut descriptions"}
        </span>
      </label>
    </section>

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

  .shortcut-global-toggles {
    margin-top: -0.25rem;
    border: 1px solid var(--sl-color-gray-5);
    border-radius: 0.5rem;
    padding: 0.4rem 0.75rem;
    background: color-mix(in srgb, var(--sl-color-bg) 88%, var(--sl-color-gray-6));
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
  }

  .description-visibility-label {
    display: inline-flex;
    align-items: center;
    gap: 0.55rem;
    cursor: pointer;
    user-select: none;
    line-height: 1.15;
  }

  .description-visibility-input {
    margin: 0;
    width: 1rem;
    height: 1rem;
  }

  .description-visibility-text {
    font-size: 0.92rem;
    font-weight: 600;
    color: var(--sl-color-text);
  }
</style>
