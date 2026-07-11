<script lang="ts">
  import {
    isStep,
    isSubstepContainer,
    type StepOrSubstep,
  } from "../../types/shortcut";
  import StepView from "./Step.svelte";
  import SubstepView from "./Substep.svelte";

  export let step: StepOrSubstep;
  export let inline: boolean = false;
</script>

<div
  class="dc-step-card self-start rounded-md border"
  class:px-1={inline}
  class:px-2={!inline}
  class:py-1={!inline}
  class:inline-block={inline}
  class:dc-step-card--substeps={isSubstepContainer(step) && !inline}
>
  {#if isStep(step)}
    <StepView bind:step bind:inline />
  {:else if isSubstepContainer(step)}
    <SubstepView bind:step bind:inline />
  {/if}
</div>

<style>
  .dc-step-card {
    box-sizing: border-box;
    max-width: 100%;
    min-width: 0;
    border-color: var(--dc-step-border, rgb(103 118 143 / 0.5));
    background: var(--dc-step-bg, rgb(13 18 30 / 0.45));
  }

  .dc-step-card--substeps {
    width: 100%;
  }
</style>
