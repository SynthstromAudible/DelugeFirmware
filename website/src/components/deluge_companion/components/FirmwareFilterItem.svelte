<script lang="ts">
  import { activeFirmware } from "../stores/firmware_store.js"
  import type { Firmware } from "../types/shortcut.js"

  export let firmware: Firmware

  const firmwareClassByColor: Record<string, string> = {
    neutral: "dc-firmware-neutral",
    purple: "dc-firmware-purple",
  }

  // Derive active state and color class from selected firmware + map.
  $: isActive = $activeFirmware != null && $activeFirmware === firmware.id
  $: colorClass = firmwareClassByColor[firmware.color] ?? "dc-firmware-neutral"
  $: classes = `dc-filter-chip ${colorClass} ${isActive ? "is-active" : ""}`

  // Toggle selected firmware filter chip.
  // Returns void after updating active firmware store.
  function onClick() {
    activeFirmware.update((oldValue) => {
      // Branch: clicking active chip clears selection.
      if (oldValue === firmware.id) {
        return null
      }

      // Branch: select this firmware id.
      return firmware.id
    })
  }
</script>

<!-- Firmware filter chip. -->
<button class={classes} on:click={onClick}>
  {firmware.title}
</button>

<style>
  .dc-filter-chip {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    margin: 0;
    white-space: nowrap;
    border-radius: 9999px;
    border: 1px solid var(--dc-chip-border);
    box-sizing: border-box;
    height: 1.875rem;
    min-height: 1.875rem;
    padding: 0 0.75rem;
    font-size: 0.875rem;
    line-height: normal;
    font-weight: 500;
    color: var(--dc-chip-border);
    background: transparent;
  }

  .dc-filter-chip.is-active {
    background-color: var(--dc-chip-bg);
    border-color: var(--dc-chip-border);
    color: var(--dc-chip-fg);
  }

  .dc-filter-chip:hover:not(.is-active) {
    background: color-mix(in srgb, var(--dc-chip-border) 20%, transparent);
    box-shadow: 0 0 0 1px color-mix(in srgb, var(--dc-chip-border) 35%, transparent) inset;
  }

  .dc-firmware-neutral {
    --dc-chip-bg: rgb(167 177 185);
    --dc-chip-border: rgb(97 111 121);
    --dc-chip-fg: rgb(34 38 42);
  }

  .dc-firmware-purple {
    --dc-chip-bg: rgb(226 187 236);
    --dc-chip-border: rgb(192 115 210);
    --dc-chip-fg: rgb(58 18 64);
  }
</style>
