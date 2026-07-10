<script lang="ts">
  import StepView from "./Step.svelte";
  import { Action } from "../../data/actions";
  import { Control } from "../../data/targets";
  import type { Step } from "../../types/shortcut";
  import type { SubstepContainer } from "../../types/shortcut";

  export let step: SubstepContainer;
  export let inline: boolean;

  type RenderedSubstep = {
    step: Step;
    compositeAction: "none" | "press-turn";
  };

  const knobControls = new Set<Control>([
    Control.X,
    Control.Y,
    Control.SELECT,
    Control.TEMPO,
    Control.PARAMETER,
    Control.LOWER_PARAM,
    Control.UPPER_PARAM,
  ]);

  const isPressTurnPair = (first: Step | undefined, second: Step | undefined) =>
    !!first &&
    !!second &&
    first.action === Action.PRESS &&
    second.action === Action.TURN &&
    first.control === second.control &&
    knobControls.has(first.control);

  $: renderedSubsteps = step.substeps.reduce<RenderedSubstep[]>((acc, _, idx, all) => {
    const current = all[idx];
    const next = all[idx + 1];

    if (isPressTurnPair(current, next)) {
      acc.push({ step: next, compositeAction: "press-turn" });
      return acc;
    }

    if (isPressTurnPair(all[idx - 1], current)) {
      return acc;
    }

    acc.push({ step: current, compositeAction: "none" });
    return acc;
  }, []);
</script>

<div class="substeps-row flex items-start gap-2">
  {#each renderedSubsteps as renderedSubstep, idx}
    {#if idx > 0}
      <span class="substeps-plus">+</span>
    {/if}
    <StepView step={renderedSubstep.step} {inline} compositeAction={renderedSubstep.compositeAction} />
  {/each}
</div>

<style>
  .substeps-plus {
    align-self: end;
    font-size: var(--dc-step-font-size, 0.9rem);
    line-height: 1;
    padding-bottom: 0.15rem;
  }
</style>
