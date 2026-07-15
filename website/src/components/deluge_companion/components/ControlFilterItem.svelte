<script lang="ts">
  import { activeControl, type ShortcutControlFilter } from "../stores/control_store.js"
  import { activeFirmware } from "../stores/firmware_store.js"
  import {
    activeShortcutCapability,
    activeShortcutSubCapability,
    activeShortcutSubSubCapability,
  } from "../stores/capability_store.js"
  import { activeView } from "../stores/view_store.js"

  export let control: ShortcutControlFilter;

  // Active state for chip styling.
  $: isActive = $activeControl === control.id;

  // Toggle control selection and clear other facet groups.
  // Returns void after updating shared filter stores.
  function onClick() {
    // Keep control facet mutually exclusive with firmware/view/capability facets.
    activeFirmware.set(null)
    activeShortcutCapability.set(null)
    activeShortcutSubCapability.set(null)
    activeShortcutSubSubCapability.set(null)
    activeView.set(null)

    activeControl.update((oldValue) => {
      // Branch: clicking active chip clears control filter.
      if (oldValue === control.id) {
        return null;
      }

      // Branch: select this control id.
      return control.id;
    });
  }
</script>

<!-- Control filter chip. -->
<button class:active={isActive} class="dc-filter-chip dc-control-chip" on:click={onClick}>
  {control.title}
</button>

<style>
  .dc-control-chip {
    --dc-control-bg: rgb(178 205 222);
    --dc-control-border: rgb(106 148 187);
    --dc-control-fg: rgb(41 50 66);

    display: inline-flex;
    align-items: center;
    justify-content: center;
    margin: 0;
    white-space: nowrap;
    border-radius: 9999px;
    border: 1px solid var(--dc-control-border);
    box-sizing: border-box;
    height: 1.875rem;
    min-height: 1.875rem;
    padding: 0 0.75rem;
    font-size: 0.875rem;
    line-height: normal;
    font-weight: 500;
    color: var(--dc-control-border);
    background: transparent;
  }

  .dc-control-chip.active {
    background-color: var(--dc-control-bg);
    border-color: var(--dc-control-border);
    color: var(--dc-control-fg);
  }

  .dc-control-chip:hover {
    filter: brightness(1.05);
  }

  .dc-control-chip:focus-visible {
    outline: 2px solid var(--dc-control-border);
    outline-offset: 2px;
  }

  .dc-control-chip:active {
    transform: translateY(1px);
  }
</style>
