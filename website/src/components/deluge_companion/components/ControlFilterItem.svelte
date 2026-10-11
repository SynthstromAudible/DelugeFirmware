<script lang="ts">
  import { activeControl, type ShortcutControlFilter } from "../stores/control_store.js"

  export let control: ShortcutControlFilter;

  // Active state for chip styling.
  $: isActive = $activeControl.has(control.id);

  // Toggle control selection.
  function onClick() {
    activeControl.update((prev) => {
      const next = new Set(prev);
      // Branch: clicking active chip removes it from selection.
      if (next.has(control.id)) {
        next.delete(control.id);
      } else {
        // Branch: add this control id to the selection.
        next.add(control.id);
      }
      return next;
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

  .dc-control-chip:hover:not(.active) {
    background: color-mix(in srgb, var(--dc-control-border) 20%, transparent);
    box-shadow: 0 0 0 1px color-mix(in srgb, var(--dc-control-border) 35%, transparent) inset;
  }

  .dc-control-chip.active:hover {
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
