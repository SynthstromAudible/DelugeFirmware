<script lang="ts">
  import {
    activeShortcutSubCapability,
    activeShortcutSubSubCapability,
    type ShortcutSubCapability,
  } from "../stores/capability_store.js";

  export let subCapability: ShortcutSubCapability;

  // Active state for chip styling.
  $: isActive = $activeShortcutSubCapability === subCapability.id;

  // Toggle selected sub-capability and clear sub-sub-capability.
  // Returns void after updating shared capability stores.
  function onClick() {
    activeShortcutSubCapability.update((oldValue) => {
      // Branch: clicking active chip clears current selection.
      if (oldValue === subCapability.id) {
        activeShortcutSubSubCapability.set(null)
        return null;
      }

      // Branch: selecting new sub-capability resets deeper facet.
      activeShortcutSubSubCapability.set(null)
      return subCapability.id;
    });
  }
</script>

<!-- Sub-capability filter chip. -->
<button class:active={isActive} class="dc-sub-capability-chip" on:click={onClick}>
  {subCapability.title}
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
