<script>
  import CompanionApp from "./deluge_companion/App.svelte"
  import "./deluge_companion/app.postcss"
</script>

<div class="not-prose my-6 overflow-hidden rounded-xl border border-[var(--sl-color-gray-5)] bg-[var(--sl-color-bg-nav)]/50 p-3 shadow-sm">
  <div class="overflow-hidden rounded-lg border border-[var(--sl-color-gray-5)] bg-[var(--sl-color-bg)]">
    <CompanionApp />
  </div>
</div>
