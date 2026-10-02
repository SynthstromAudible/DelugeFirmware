<script>
  import { setAllShortcutTagsVisible } from "../stores/shortcut_tag_visibility_store.js";
  import { setAllShortcutDescriptionsVisible } from "../stores/shortcut_description_visibility_store.js";

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

<style>
  .shortcut-global-toggles {
    margin: 0.8rem 0 0.3rem;
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
