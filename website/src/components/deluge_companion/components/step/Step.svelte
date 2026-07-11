<script lang="ts">
  import ControlView from "./StepControl.svelte";
  import ActionView from "./StepAction.svelte";
  import type { Step } from "../../types/shortcut";

  export let step: Step;
  export let inline: boolean = false;
  export let compositeAction: "none" | "press-turn" = "none";
</script>

<div class="step step grid items-baseline justify-items-center gap-x-1 text-[0.9rem]">
  <ActionView {step} {compositeAction} />
  <ControlView {step} {inline} {compositeAction} />
</div>

<style>
  .step {
    --dc-step-font-size: 0.9rem;
    --dc-step-action-size: 1rem;
    --dc-step-icon-size: 1.5rem;
    --dc-step-icon-gap: 0.19rem;
    --dc-step-icon-row-size: var(--dc-step-icon-size);
    grid-template-columns: auto;
    grid-template-rows: var(--dc-step-icon-row-size) auto;
    row-gap: var(--dc-step-icon-gap);
    grid-template-areas:
      ". target-icon"
      "action target-title";
  }
</style>
