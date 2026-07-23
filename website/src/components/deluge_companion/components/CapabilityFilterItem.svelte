<script lang="ts">
  import {
    activeShortcutCapability,
    activeShortcutSubCapability,
    activeShortcutSubSubCapability,
    type ShortcutCapability,
  } from "../stores/capability_store.js";

  export let capability: ShortcutCapability;

  // Active state for chip styling/aria semantics.
  $: isActive = $activeShortcutCapability === capability.id;

  // Toggle selected capability and clear dependent facet levels.
  // Returns void after updating shared capability stores.
  function onClick() {
    activeShortcutCapability.update((oldValue) => {
      // Branch: clicking active chip clears capability selection.
      if (oldValue === capability.id) {
        activeShortcutSubCapability.set(null)
        activeShortcutSubSubCapability.set(null)
        return null;
      }

      // Branch: selecting a new capability resets child facet selections.
      activeShortcutSubCapability.set(null)
      activeShortcutSubSubCapability.set(null)
      return capability.id;
    });
  }
</script>

<!-- Capability filter chip. -->
<button class:active={isActive} class="dc-capability-chip" on:click={onClick}>
  {capability.title}
</button>

<style>
  .dc-capability-chip {
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

  .dc-capability-chip.active {
    background: var(--dc-capability-bg);
    border-color: var(--dc-capability-border);
    color: var(--dc-capability-fg);
  }

  .dc-capability-chip:hover:not(.active) {
    background: color-mix(in srgb, var(--dc-capability-border) 20%, transparent);
    box-shadow: 0 0 0 1px color-mix(in srgb, var(--dc-capability-border) 35%, transparent) inset;
  }
</style>
