<script lang="ts">
  import {
    activeShortcutSubSubCapability,
    type ShortcutSubSubCapability,
  } from "../stores/capability_store.js";

  export let subSubCapability: ShortcutSubSubCapability;

  // Active state for chip styling.
  $: isActive = $activeShortcutSubSubCapability === subSubCapability.id;

  // Toggle selected sub-sub-capability.
  // Returns void after updating the active sub-sub-capability store.
  function onClick() {
    activeShortcutSubSubCapability.update((oldValue) => {
      // Branch: clicking active chip clears selection.
      if (oldValue === subSubCapability.id) {
        return null;
      }

      // Branch: select this sub-sub-capability.
      return subSubCapability.id;
    });
  }
</script>

<!-- Sub-sub-capability filter chip. -->
<button class:active={isActive} class="dc-sub-capability-chip" on:click={onClick}>
  {subSubCapability.title}
</button>

<style>
  .dc-sub-capability-chip {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    margin: 0;
    white-space: nowrap;
    border-radius: 0.375rem;
    border: 1px solid var(--dc-capability-border);
    box-sizing: border-box;
    min-height: 1.875rem;
    padding: 0 0.75rem;
    font-size: 0.875rem;
    line-height: normal;
    font-weight: 500;
    color: var(--dc-capability-border);
    background: transparent;
  }

  .dc-sub-capability-chip.active {
    background: var(--dc-capability-bg);
    border-color: var(--dc-capability-border);
    color: var(--dc-capability-fg);
  }

  .dc-sub-capability-chip:hover:not(.active) {
    background: color-mix(in srgb, var(--dc-capability-border) 20%, transparent);
    box-shadow: 0 0 0 1px color-mix(in srgb, var(--dc-capability-border) 35%, transparent) inset;
  }
</style>
